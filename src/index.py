#!/usr/bin/env python
from flask import Flask, render_template, request, redirect
import os
from datetime import datetime

app = Flask(__name__)
@app.route('/')
def index():
	return render_template("Mesin.html")
if __name__ == '__main__':

	app.run(host='0.0.0.0', port =5000)
