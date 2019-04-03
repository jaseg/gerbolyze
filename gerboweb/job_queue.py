
import json
import sqlite3

class JobQueue:
    def __init__(self, dbfile):
        self.dbfile = dbfile
        self.db = sqlite3.connect(dbfile, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        with self.db as conn:
            conn.execute('''CREATE TABLE IF NOT EXISTS jobs
                    (id INTEGER PRIMARY KEY,
                    type TEXT,
                    params TEXT,
                    client TEXT,
                    result INTEGER DEFAULT NULL,
                    created DATETIME DEFAULT CURRENT_TIMESTAMP,
                    consumed DATETIME DEFAULT NULL,
                    aborted DATETIME DEFAULT NULL,
                    finished DATETIME DEFAULT NULL);''')
    
    def enqueue(self, task_type:str, client, **params):
        """ Enqueue a job of the given type with the given params. Returns the new job ID. """
        with self.db as conn:
            return conn.execute('INSERT INTO jobs(type, client, params) VALUES (?, ?, ?)',
                    (task_type, client, json.dumps(params))).lastrowid

    def pop(self, task_type):
        """ Fetch the next job of the given type. Returns a sqlite3.Row object of the job or None if no jobs of the given
        type are queued. """
        with self.db as conn:
            job = conn.execute('SELECT * FROM jobs WHERE type=? AND consumed IS NULL AND aborted IS NULL ORDER BY created ASC LIMIT 1',
                    (task_type,)).fetchone()
            if job is None:
                return None

            # Atomically commit to this job
            conn.execute('UPDATE jobs SET consumed=datetime("now") WHERE id=?', (job['id'],))

            return Job(self.db, job)

    def job_iter(self, task_type):
        return iter(lambda: self.pop(task_type), None)

    def __getitem__(self, key):
        """ Return the job with the given ID, or raise a KeyError if the key cannot be found. """
        with self.db as conn:
            job = conn.execute('SELECT * FROM jobs WHERE id=?', (key,)).fetchone()
            if job is None:
                raise KeyError(f'Unknown job ID "{key}"')

            return Job(self.db, job)

class Job(dict):
    def __init__(self, db, row):
        super().__init__(json.loads(row['params']))
        self._db = db
        self._row = row
        self.id = row['id']
        self.type = row['type']
        self.client = row['client']
        self.created = row['created']
        self.consumed = row['consumed']
        self.finished = row['finished']
        self.result = row['result']

    def __enter__(self):
        return self

    def __exit__(self, _exc_type, _exc_val, _exc_tb):
        with self._db as conn:
            conn.execute('UPDATE jobs SET finished=datetime("now"), result=? WHERE id=?', (self.result, self.id,))

    def abort(self, job_id):
        with self.db as conn:
            conn.execute('UPDATE jobs SET aborted=datetime("now") WHERE id=?', (self.id,))

