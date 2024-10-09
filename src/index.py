#!/usr/bin/env python
from flask import Flask, render_template, request, redirect
import os, logging
from datetime import datetime

app = Flask(__name__)
@app.route('/', methods=['GET', 'POST'])
def index():
	user_agent = request.headers.get('User-Agent')
	user_agent = user_agent.lower()
	#app.logger.info(user_agent)
	if "iphone" in user_agent or "android" in user_agent:
		return render_template("Mobile.html")
	return render_template("Mesin.html")
if __name__ == '__main__':
	from waitress import serve
	serve(app,host='0.0.0.0',port=5000)
	#app.run(host='0.0.0.0', port =5000,debug=True)
