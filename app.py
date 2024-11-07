import io
import os
from dotenv import load_dotenv

from flask import Flask, render_template, request
from openai import OpenAI

load_dotenv()
api_key = os.environ.get('OPENAI_API_KEY')

client = OpenAI()

app = Flask(__name__)

@app.route("/")
def home():
    return render_template("index.html")

@app.route('/hello')
def hello_world():
    return 'Hello World!'

@app.route("/transcribe", methods=["POST"])
def transcribe():
    # audio_path = "Test.m4a"
    # file = open(audio_path, "rb")
    file = request.files["audio"]
    buffer = io.BytesIO(file.read())
    buffer.name = "audio.m"

    transcript = client.audio.transcriptions.create(
        model="whisper-1",
        file=buffer
    )
    return {"output": transcript.text}

# whisper api 작동 확인 코드 
@app.route("/whisper-api")
def whisper_test():
    audio_path = "./Test.m4a"
    file = open(audio_path, "rb")

    transcript = client.audio.transcriptions.create(
        model="whisper-1",
        file=file
    )

    response = transcript.text
    # print(response)
    return response

if __name__ == "__main__":
    #app.run(debug=True)
    app.run('0.0.0.0', port=5001, debug=True)