#!/usr/bin/env python3

# TODO setup webserver user disk quota

import tempfile
import uuid
from functools import wraps
from os import path
from pathlib import Path
import os
import sqlite3

from flask import Flask, url_for, redirect, session, make_response, render_template, request, send_file, abort, flash
from flask_wtf import FlaskForm
from flask_wtf.file import FileField, FileRequired
from wtforms.fields import RadioField
from wtforms.validators import DataRequired
from werkzeug.utils import secure_filename
import uwsgidecorators

from job_queue import JobQueue
import job_processor

app = Flask(__name__, static_url_path='/gerboweb/static')
app.config.from_envvar('GERBOWEB_SETTINGS')
if app.config['SECRET_KEY'] is None:
    if (p := Path('/run/secrets/gerboweb')).is_file():
        app.config['SECRET_KEY'] = p.read_bytes()
    else:
        app.config['SECRET_KEY'] = os.urandom(32)

class UploadForm(FlaskForm):
    upload_file = FileField(validators=[DataRequired()])

class OverlayForm(UploadForm):
    upload_file = FileField(validators=[FileRequired()])

class ResetForm(FlaskForm):
    pass

job_queue = JobQueue(app.config['JOB_QUEUE_DB'])

@uwsgidecorators.timer(1)
def job_processor_timer(_num):
    job_processor.process_job(job_queue)

def tempfile_path(namespace):
    """ Return a path for a per-session temporary file identified by the given namespace. Create the session tempfile
    dir if necessary. The application tempfile dir is controlled via the upload_path config value and not managed by
    this function. """
    if not path.isdir(app.config['UPLOAD_PATH']):
        os.mkdir(app.config['UPLOAD_PATH'])
    sess_tmp = path.join(app.config['UPLOAD_PATH'], session['session_id'])
    if not path.isdir(sess_tmp):
        os.mkdir(sess_tmp)

    return path.join(sess_tmp, namespace)

def require_session_id(fun):
    @wraps(fun)
    def wrapper(*args, **kwargs):
        if 'session_id' not in session:
            session['session_id'] = str(uuid.uuid4())
        return fun(*args, **kwargs)
    return wrapper

@app.route('/')
@require_session_id
def index():
    forms = {
            'gerber_form': UploadForm(),
            'overlay_form': OverlayForm(),
            'reset_form': ResetForm() }

    for job_type in ('vector_job', 'render_job'):
        if job_type in session:
            try:
                job = job_queue[session[job_type]]
                if job.finished:
                    if not job.result:
                        flash(f'Error processing gerber files', 'success') # FIXME make this an error, add CSS
                    del session[job_type]
            except:
                session.clear()

    r = make_response(render_template('index.html',
            has_renders = path.isfile(tempfile_path('gerber.zip')),
            has_output = path.isfile(tempfile_path('overlay.svg')),
             **forms))
    if 'vector_job' in session or 'render_job' in session:
        r.headers.set('refresh', '10')
    return r

# NOTES about the gerber and overlay file upload routines
#  * The maximum upload size is limited by the MAX_CONTENT_LENGTH config setting.
#  * The uploaded files are deleted after a while by systemd tmpfiles.d
# TODO: validate this setting applies *after* gzip transport compression

def vectorize():
    if 'vector_job' in session:
        try:
            job_queue[session['vector_job']].abort()
        except:
            pass
    session['vector_job'] = job_queue.enqueue('vector',
            client=request.remote_addr,
            session_id=session['session_id'],
            gerber_in=tempfile_path('gerber.zip'),
            overlay=tempfile_path('overlay.svg'),
            gerber_out=tempfile_path('gerber_out.zip'))

def render():
    if 'render_job' in session:
        try:
            job_queue[session['render_job']].abort()
        except:
            pass
    session['render_job'] = job_queue.enqueue('render',
            session_id=session['session_id'],
            infile=tempfile_path('gerber.zip'),
            preview_top_out=tempfile_path('preview_top.png'),
            preview_bottom_out=tempfile_path('preview_bottom.png'),
            template_top_out=tempfile_path('template_top.svg'),
            template_bottom_out=tempfile_path('template_bottom.svg'),
            client=request.remote_addr)

@app.route('/upload/gerber', methods=['POST'])
@require_session_id
def upload_gerber():
    upload_form = UploadForm()
    if upload_form.validate_on_submit():
        f = upload_form.upload_file.data
        f.save(tempfile_path('gerber.zip'))
        session['filename'] = secure_filename(f.filename) # Cache filename for later download

        render()
        if path.isfile(tempfile_path('overlay.svg')): # Re-vectorize when gerbers change
            vectorize()

        flash(f'Gerber file successfully uploaded.', 'success')
    return redirect(url_for('index'))

@app.route('/upload/overlay', methods=['POST'])
@require_session_id
def upload_overlay():
    upload_form = OverlayForm()
    if upload_form.validate_on_submit():
        f = upload_form.upload_file.data
        f.save(tempfile_path('overlay.svg'))
        vectorize()

        flash(f'Overlay file successfully uploaded.', 'success')
    return redirect(url_for('index'))

@app.route('/render/preview/<side>')
def render_preview(side):
    if not side in ('top', 'bottom'):
        return abort(400, 'side must be either "top" or "bottom"')
    return send_file(tempfile_path(f'preview_{side}.png'))

@app.route('/render/download/<side>')
def render_download(side):
    if not side in ('top', 'bottom'):
        return abort(400, 'side must be either "top" or "bottom"')

    session['last_download'] = side
    return send_file(tempfile_path(f'template_{side}.svg'),
            mimetype='image/svg',
            as_attachment=True,
            attachment_filename=f'{path.splitext(session["filename"])[0]}_template_{side}.svg')

@app.route('/output/download')
def output_download():
    return send_file(tempfile_path('gerber_out.zip'),
            mimetype='application/zip',
            as_attachment=True,
            attachment_filename=f'{path.splitext(session["filename"])[0]}_with_artwork.zip')

@app.route('/session_reset', methods=['POST'])
@require_session_id
def session_reset():
    if 'render_job' in session:
        try:
            job_queue[session['render_job']].abort()
        except:
            pass
    if 'vector_job' in session:
        try:
            job_queue[session['vector_job']].abort()
        except:
            pass
    session.clear()
    flash('Session reset', 'success');
    return redirect(url_for('index'))

