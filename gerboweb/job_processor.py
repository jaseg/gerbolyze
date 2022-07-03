
import signal
import subprocess
import logging
import itertools
import tempfile

from job_queue import JobQueue


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
    
    signal.signal(signal.SIGALRM, lambda *args: None) # Ignore incoming alarm signals while processing jobs
    signal.setitimer(signal.ITIMER_REAL, 0.001, 1)
    while signal.sigwait([signal.SIGALRM, signal.SIGINT]) == signal.SIGALRM:
        logging.debug('Checking for jobs')
        for job in job_queue.job_iter('render'):
            logging.info(f'Processing {job.type} job {job.id} session {job["session_id"]} from {job.client} submitted {job.created}')
            with job:
                try:
                    with tempfile.NamedTemporaryFile(suffix='.svg') as svg:
                        subprocess.run(['python3', '-m', 'gerbonara', '--top', job['infile'], svg.name], check=True)
                        subprocess.run(['resvg', '--dpi', '300', svg.name, job['preview_top_out']], check=True)
                    with tempfile.NamedTemporaryFile(suffix='.svg') as svg:
                        subprocess.run(['python3', '-m', 'gerbonara', '--bottom', job['infile'], svg.name], check=True)
                        subprocess.run(['resvg', '--dpi', '300', svg.name, job['preview_bottom_out']], check=True)
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
    logging.info('Caught SIGINT. Exiting.')

