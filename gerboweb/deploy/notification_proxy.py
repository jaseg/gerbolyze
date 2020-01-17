import smtplib
import ssl
import email.utils
import hmac
from email.mime.text import MIMEText
from datetime import datetime
import functools
import json
import binascii

from flask import Flask, request, abort

app = Flask(__name__)
app.config.from_pyfile('config.py')

smtp_server = "smtp.sendgrid.net"
port = 465

mail_routes = {}
def mail_route(name, receiver, subject):
    def wrap(func):
        global routes
        mail_routes[name] = (receiver, subject, func)
        return func
    return wrap


def authenticate(secret):
    def wrap(func):
        func.last_seqnum = 0
        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            if not request.is_json:
                abort(400)

            if not 'auth' in request.json and 'payload' in request.json:
                abort(400)

            if not isinstance(request.json['auth'], str):
                abort(400)
            their_digest = binascii.unhexlify(request.json['auth'])

            our_digest = hmac.digest(secret.encode('utf-8'), request.json['payload'].encode('utf-8'), 'sha256')
            if not hmac.compare_digest(their_digest, our_digest):
                abort(403)

            try:
                payload = json.loads(request.json['payload'])
            except:
                abort(400)

            if not isinstance(payload['seq'], int) or payload['seq'] <= func.last_seqnum:
                abort(400)

            func.last_seqnum = payload['seq']
            del payload['seq']
            return func(payload)
        return wrapper
    return wrap

@mail_route('klingel', 'computerstuff@jaseg.de', 'It rang!')
@authenticate(app.config['SECRET_KLINGEL'])
def klingel(_):
    return f'Date: {datetime.utcnow().isoformat()}'


@app.route('/notify/<route_name>', methods=['POST'])
def notify(route_name):
    try:
        context = ssl.create_default_context()
        smtp = smtplib.SMTP_SSL(smtp_server, port)
        smtp.login('apikey', app.config['SENDGRID_APIKEY'])

        sender = f'{route_name}@{app.config["DOMAIN"]}'

        receiver, subject, func = mail_routes[route_name]

        msg = MIMEText(func() or subject)
        msg['Subject'] = subject
        msg['From'] = sender
        msg['To'] = receiver
        msg['Date'] = email.utils.formatdate()
        smtp.sendmail(sender, receiver, msg.as_string())
    finally:
        smtp.quit() 
    return 'success'

if __name__ == '__main__':
    app.run()
