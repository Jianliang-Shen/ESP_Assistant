from flask import Flask, send_file
import os

app = Flask(__name__)
script_dir = os.path.dirname(os.path.abspath(__file__))

@app.route('/get_wav', methods=['GET'])
def get_wav():
    return send_file(f'{script_dir}/output.wav', mimetype='audio/wav')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
