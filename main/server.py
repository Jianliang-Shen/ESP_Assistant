from flask import Flask, request, jsonify, send_file
import os
import ollama
import requests
import torch
import torchaudio
import numpy as np
import simpleaudio as sa
import argparse
import logging
import requests
import torch
import torchaudio
import numpy as np

import ffmpeg

def convert_wav(input_file, output_file):
    try:
        # 使用ffmpeg将WAV文件转换为指定参数
        ffmpeg.input(input_file).output(output_file, ar=16000, ac=2, sample_fmt='s16').run()
        print(f"Successfully converted {input_file} to {output_file}")
    except ffmpeg.Error as e:
        print(f"Error occurred: {e.stderr.decode()}")


messages = []
script_dir = os.path.dirname(os.path.abspath(__file__))

app = Flask(__name__)

def send_to_cosyvoice(txt):
    target_sr = 22050
    url = "http://{}:{}/inference_{}".format('localhost', '50000', 'sft')

    payload = {
            'tts_text': txt,
            'spk_id': '中文女'
        }
    response = requests.request("GET", url, data=payload, stream=True)
    
    tts_audio = b''
    for r in response.iter_content(chunk_size=16000):
        tts_audio += r
    tts_speech = torch.from_numpy(np.array(np.frombuffer(tts_audio, dtype=np.int16))).unsqueeze(dim=0)

    torchaudio.save(f"{script_dir}/demo.wav", tts_speech, target_sr)
    # wave_obj = sa.WaveObject.from_wave_file(f"{script_dir}/demo.wav")
    # play_obj = wave_obj.play()
    # play_obj.wait_done()

@app.route('/upload', methods=['POST'])
def upload_audio():
    chunk_data = request.data
    print(f"Received chunk of length: {len(chunk_data)}")

    if len(chunk_data) == 0:
        return "No data received", 400

    # 将块写入文件
    with open(f"{script_dir}/received_audio.wav", "ab") as f:  # 使用追加模式写入
        f.write(chunk_data)

    return "Chunk received", 200

@app.route('/get_response', methods=['GET'])
def get_response():
    os.system(f'python {script_dir}/funasr.py --host "127.0.0.1" --port 10095 --mode offline --audio_in "{script_dir}/received_audio.wav" --output_dir "{script_dir}"')
    print("[ OK ] FunASR Down")
    
    os.remove(f'{script_dir}/received_audio.wav')
    print("[ OK ] Delete Audio file")

    with open(f'{script_dir}/asr_result.txt', 'r', encoding='utf-8') as file:
        content = file.read()

    print("[ OK ] Read and return Asr result")
    return content[:-1], 200

@app.route('/get_response2', methods=['GET'])
def get_response2():

    with open(f'{script_dir}/asr_result.txt', 'r', encoding='utf-8') as file:
        prompt = file.read()
    print("[ OK ] Read prompt")

    messages.append({'role': 'user', 'content': prompt})

    client = ollama.Client(host='http://localhost:11434')
    stream = client.chat(model='assistant', messages=messages)
    print("[ OK ] Connect ollama")

    assistant_log = stream['message']['content']
    # print(f'Ollama response is {assistant_log}')
    print("[ OK ] Get Ollama result")

    # for chunk in stream:
    #     assistant_log += chunk['message']['content']
        # print(chunk['message']['content'], end='', flush=True)
    messages.append({'role': 'assistant', 'content': assistant_log})

    with open(f'{script_dir}/ollama_result.txt', 'w', encoding='utf-8') as file:
        file.write(assistant_log)
    
    send_to_cosyvoice(assistant_log)

    return assistant_log, 200

@app.route('/get_mp3', methods=['GET'])
def get_wav():
    with open(f'{script_dir}/ollama_result.txt', 'r', encoding='utf-8') as file:
        tts_txt = file.read()
    print("[ OK ] Read ollama result")

    send_to_cosyvoice(tts_txt)

    audio_file_path = f"{script_dir}/demo.wav"

    # 检查文件是否存在
    if not os.path.exists(audio_file_path):
        return jsonify({"error": "Audio file not found!"}), 404

    if os.path.exists(f'{script_dir}/output.wav'):
        os.remove(f'{script_dir}/output.wav')

    convert_wav(audio_file_path, f'{script_dir}/output.wav')

    return send_file(f'{script_dir}/output.wav', mimetype='audio/wav')

if __name__ == '__main__':
    modelfile='''
    FROM llama3.1
    SYSTEM 你是一个语音助手，回复必须不超过二十个字，如果用户让你讲一个很长的故事，你会回答用户，"对不起，我无法生成长文字"
    '''

    ollama.create(model='assistant', modelfile=modelfile)
    print("[ OK ] Create Ollama assistant model and set system prompt")
    
    print("Starting Flask server...")
    app.run(host='0.0.0.0', port=5000, debug=True)
    print("Flask server started")
