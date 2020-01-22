import smtplib
import ssl
import email.utils
import hmac
from email.mime.text import MIMEText
from datetime import datetime
import time
import functools
import json
import binascii
import uwsgidecorators

import sqlite3

from flask import Flask, request, abort

app = Flask(__name__)
app.config.from_pyfile('config.py')

db = sqlite3.connect(app.config['SQLITE_DB'], check_same_thread=False)
with db as conn:
    conn.execute('''CREATE TABLE IF NOT EXISTS seqs_seen
            (route_name TEXT PRIMARY KEY,
            seq INTEGER)''')
    conn.execute('''CREATE TABLE IF NOT EXISTS time_seen
            (route_name TEXT PRIMARY KEY)''')

    conn.execute('''CREATE TABLE IF NOT EXISTS heartbeats_seen
            (route_name TEXT PRIMARY KEY,
            timestamp INTEGER,
            notified INTEGER)''')
    # Clear table on startup to avoid spurious notifications
    conn.execute('''DELETE FROM heartbeats_seen''')

mail_routes = {}

def mail_route(name, receiver, subject, secret):
    def wrap(func):
        global routes
        mail_routes[name] = (receiver, subject, func, secret)
        return func
    return wrap


def authenticate(route_name, secret, clock_delta_tolerance:'s'=120):
    with db as conn:
        if not request.is_json:
            print('Rejecting notification: Incorrect content type')
            abort(400)

        if not 'auth' in request.json and 'payload' in request.json:
            print('Rejecting notification: signature or payload not found')
            abort(400)

        if not isinstance(request.json['auth'], str):
            print('Rejecting notification: signature is of incorrect type')
            abort(400)
        their_digest = binascii.unhexlify(request.json['auth'])

        our_digest = hmac.digest(secret.encode('utf-8'), request.json['payload'].encode('utf-8'), 'sha256')
        if not hmac.compare_digest(their_digest, our_digest):
            print('Rejecting notification: Incorrect signature')
            abort(403)

        try:
            payload = json.loads(request.json['payload'])
        except:
            print('Rejecting notification: Payload is not JSON')
            abort(400)

        last_seqnum = conn.execute('SELECT seq FROM seqs_seen WHERE route_name = ?', (route_name,)).fetchone() or 0
        # We can check for seq here: Only an attacker with knowledge of the secret would be able to remove
        # seq from a message. This means for a single key, only messages with or without seq may ever be used.
        if 'seq' in payload:
            seq = payload['seq']
            if not isinstance(seq, int):
                print('Rejecting notification: seq of wrong type')
                abort(400)

            if seq <= last_seqnum:
                print('Rejecting notification: seq out of order')
                abort(400)

            conn.execute('INSERT OR REPLACE INTO seqs_seen VALUES (?, ?)', (route_name, seq))

        elif last_seqnum:
            print('Rejecting notification: seq not included but past messages included seq')
            abort(400)

        msg_time = None
        if 'time' in payload:
            msg_time = payload['time']
            if not isinstance(msg_time, int):
                print('Rejecting notification: time of wrong type')
                abort(400)

            if abs(msg_time - int(time.time())) > clock_delta_tolerance:
                print('Rejecting notification: timestamp too far in the future or past')
                abort(400)

            conn.execute('INSERT OR REPLACE INTO time_seen VALUES (?)', (route_name,))

        elif conn.execute('SELECT * FROM time_seen WHERE route_name = ?', (route_name,)).fetchone():
            print('Rejecting notification: time not included but past messages included time')
            abort(400)

        if msg_time is None:
            msg_time = int(time.time())

        return msg_time, payload['scope'], payload['d']

@mail_route('klingel', 'computerstuff@jaseg.de', 'It rang!', app.config['SECRET_KLINGEL'])
def klingel(rms=None, capture=None, **kwargs):
    return f'rms={rms}\ncapture={capture}\nextra_args={kwargs}'


def send_mail(route_name, receiver, subject, body):
    try:
        context = ssl.create_default_context()
        smtp = smtplib.SMTP_SSL(app.config['SMTP_HOST'], app.config['SMTP_PORT'])
        smtp.login('apikey', app.config['SENDGRID_APIKEY'])

        sender = f'{route_name}@{app.config["DOMAIN"]}'

        msg = MIMEText(body)
        msg['Subject'] = subject
        msg['From'] = sender
        msg['To'] = receiver
        msg['Date'] = email.utils.formatdate()

        smtp.sendmail(sender, receiver, msg.as_string())
    finally:
        smtp.quit() 

@app.route('/v1/notify/<route_name>', methods=['POST'])
def notify(route_name):
    receiver, notify_subject, func, secret = mail_routes[route_name]
    msg_time, scope, kwargs = authenticate(route_name, secret)

    if scope == 'default':
        # Exceptions will yield a 500 error
        body = func(**kwargs)
        send_mail(route_name, receiver, notify_subject, body or 'empty message')

    elif scope == 'boot':
        formatted = datetime.utcfromtimestamp(msg_time).isoformat()
        send_mail(route_name, receiver, 'System startup', f'System powered up at {formatted}')

    elif scope == 'heartbeat':
        with db as conn:
            conn.execute('INSERT OR REPLACE INTO heartbeats_seen VALUES (?, ?, 0)', (route_name, int(time.time())))

    return 'success'

@uwsgidecorators.timer(60)
def heartbeat_timer(_uwsgi_signum):
    threshold = int(time.time()) - app.config['HEARTBEAT_TIMEOUT']
    with db as conn:
        for route, ts in db.execute(
                'SELECT route_name, timestamp FROM heartbeats_seen WHERE timestamp <= ? AND notified == 0',
                (threshold,)).fetchall():
            print(f'Heartbeat expired for {route}: {ts} < {threshold}')

            receiver, *_ = mail_routes[route]
            last = datetime.utcfromtimestamp(ts).isoformat()

            send_mail(route, receiver, 'Heartbeat timeout', f'Last heartbeat at {last}')
            db.execute('UPDATE heartbeats_seen SET notified = ? WHERE route_name = ?', (int(time.time()), route))

if __name__ == '__main__':
    app.run()

