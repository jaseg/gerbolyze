
import os
import sys
import signal
import subprocess
import logging
import itertools
import tempfile
from pathlib import Path

from job_queue import JobQueue

def run_resvg(*args):
        if 'RESVG' in os.environ:
            subprocess.run([os.environ['RESVG'], *args], check=True)

        else:
            # By default, try four options:
            for candidate in [
                    # somewhere in $PATH
                    'resvg',

                    # in user-local cargo installation
                    Path.home() / '.cargo' / 'bin' / 'resvg',

                    # somewhere in $PATH
                    'wasi-resvg',

                    # in user-local pip installation
                    Path.home() / '.local' / 'bin' / 'wasi-resvg',

                    # next to our current python interpreter (e.g. in virtualenv)
                    str(Path(sys.executable).parent / 'resvg'),
                    str(Path(sys.executable).parent / 'wasi-resvg') ]:

                try:
                    subprocess.run([candidate, *args], check=True)
                    print('used svg-flatten at', candidate)
                    break

                except (FileNotFoundError, ModuleNotFoundError):
                    continue

            else:
                raise SystemError('svg-flatten executable not found')

def process_job(job_queue):
    logging.debug('Checking for jobs')
    for job in job_queue.job_iter('render'):
        logging.info(f'Processing {job.type} job {job.id} session {job["session_id"]} from {job.client} submitted {job.created}')
        with job:
            try:
                with tempfile.NamedTemporaryFile(suffix='.svg') as svg:
                    subprocess.run(['python3', '-m', 'gerbonara', '--top', job['infile'], svg.name], check=True)
                    run_resvg('--dpi', '300', svg.name, job['preview_top_out'])
                with tempfile.NamedTemporaryFile(suffix='.svg') as svg:
                    subprocess.run(['python3', '-m', 'gerbonara', '--bottom', job['infile'], svg.name], check=True)
                    run_resvg('--dpi', '300', svg.name, job['preview_bottom_out'])
                subprocess.run(['python3', '-m', 'gerbolyze', 'template', '--top', job['infile'], job['template_top_out']], check=True)
                subprocess.run(['python3', '-m', 'gerbolyze', 'template', '--bottom', job['infile'], job['template_bottom_out']], check=True)
                logging.info(f'Finishied processing {job.type} job {job.id}')
                job.result = True
            except:
                logging.exception('Error during job processing')
                job.result = False

    for job in job_queue.job_iter('vector'):
        logging.info(f'Processing {job.type} job {job.id} session {job["session_id"]} from {job.client} submitted {job.created}')
        with job:
            try:
                subprocess.run(['python3', '-m', 'gerbolyze', 'paste', job['gerber_in'], job['overlay'], job['gerber_out']], check=True)
                logging.info(f'Finishied processing {job.type} job {job.id}')
                job.result = True
            except:
                logging.exception('Error during job processing')
                job.result = False

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('queue', help='job queue sqlite3 database file')
    parser.add_argument('--loglevel', '-l', default='info')
    args = parser.parse_args()

    numeric_level = getattr(logging, args.loglevel.upper(), None)
    if not isinstance(numeric_level, int):
        raise ValueError('Invalid log level: %s' % loglevel)
    logging.basicConfig(level=numeric_level)

    job_queue = JobQueue(args.queue)
    print('Job processor online')
    
    signal.signal(signal.SIGALRM, lambda *args: None) # Ignore incoming alarm signals while processing jobs
    signal.setitimer(signal.ITIMER_REAL, 0.001, 1)
    while signal.sigwait([signal.SIGALRM, signal.SIGINT]) == signal.SIGALRM:
        process_job(job_queue)
    logging.info('Caught SIGINT. Exiting.')

