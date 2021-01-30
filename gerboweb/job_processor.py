
import signal
import subprocess
import logging
import itertools

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
                job.result = subprocess.call(['sudo', '/usr/local/sbin/gerbolyze_render.sh', job['session_id']])
                logging.info(f'Finishied processing {job.type} job {job.id}')

        for job in job_queue.job_iter('vector'):
            logging.info(f'Processing {job.type} job {job.id} session {job["session_id"]} from {job.client} submitted {job.created}')
            with job:
                job.result = subprocess.call(['sudo', '/usr/local/sbin/gerbolyze_vector.sh', job['session_id'], job['side']])
                logging.info(f'Finishied processing {job.type} job {job.id}')
    logging.info('Caught SIGINT. Exiting.')

