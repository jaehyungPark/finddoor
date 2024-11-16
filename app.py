import io
import os
import time
from threading import Thread
import json

from dotenv import load_dotenv
from flask import Flask, Response, request, render_template, jsonify
from openai import OpenAI

load_dotenv()
api_key = os.environ.get('OPENAI_API_KEY')

client = OpenAI()

app = Flask(__name__)

# 클라이언트별 상태 저장
client_status = {}
place_dict = {"도서관 정문" : 11, # tag_name : tag_num
              "도서관 후문" : 22,
              "기숙사 정문" : 33,
              "기숙사 후문" : 44,
              "학생회관 정문" : 55,
              "학생회관 후문" : 66}

# -----------------------------------------------------
# User defined functions
# -----------------------------------------------------
"""
# 파일 재인코딩 함수
from pydub import AudioSegment
def reencode_to_mp3(file_data):
    # 바이너리 데이터로 AudioSegment 로드
    audio = AudioSegment.from_file(io.BytesIO(file_data), format="mp3")
    # 재인코딩된 MP3 데이터 생성
    output = io.BytesIO()
    audio.export(output, format="mp3")
    output.seek(0)  # 포인터를 시작으로 이동
    return output
"""

def transcribe(audio_file_path):
    try:        
        with open(audio_file_path, "rb") as file:
            # Whisper API 호출
            transcript = client.audio.transcriptions.create(
                model="whisper-1",
                file=file,
                response_format="text"
            )
            
        # API 응답에서 텍스트 추출
        return transcript  # transcript 자체가 텍스트 데이터임
        
    except Exception as e:
        # 에러 발생 시 예외 반환
        raise Exception(f"Transcription failed: {str(e)}")

def match_tag_num(text_msg):
# tag number을 결정하는 mapping algorithm. (-> LLM을 통한 처리 가능성)
    vocab_list = text_msg.split()
    if '도서관' in vocab_list:
        if '정문' in vocab_list:
            tag_num = 11
        elif '후문' in vocab_list:
            tag_num = 22
    elif '기숙사' in vocab_list:
        if '정문' in vocab_list:
            tag_num = 33
        elif '후문' in vocab_list:
            tag_num = 44
    elif '학생회관' in vocab_list:
        if '정문' in vocab_list:
            tag_num = 55
        elif '후문' in vocab_list:
            tag_num = 66
    
    return tag_num

# -----------------------------------------------------
# App Default (for testing)
# -----------------------------------------------------
@app.route("/")
def home():
    return render_template("index.html")

@app.route('/hello')
def hello_world():
    return 'Hello World!'

# -----------------------------------------------------
# Handling Tag
# -----------------------------------------------------
@app.route('/connect', methods=['POST'])
def connect():
    """
    클라이언트가 tag_num을 통해 서버와 연결.
    """
    data = request.get_json()

    if not data or 'tag_num' not in data:
        return jsonify({"error": "Missing 'tag_num' in request data"}), 400

    tag_num = data['tag_num']

    # 새로운 클라이언트 추가 또는 기존 클라이언트 갱신
    client_status[tag_num] = {"signal": None, "timestamp": time.time()}
    return jsonify({"message": "Connection established", "tag_num": tag_num}), 200

@app.route('/get_signal/<int:tag_num>', methods=['GET'])
def get_signal(tag_num):
    """
    특정 클라이언트의 신호를 조회.
    """
    if tag_num not in client_status:
        return jsonify({"error": "Invalid tag_num"}), 400

    # 클라이언트가 기다릴 수 있도록 (10분 이상 대기 가능)
    timeout = 600  # 10 minutes (= 600 sec)
    elapsed_time = 0
    interval = 10  # Polling interval

    while elapsed_time < timeout:
        # 이 while 문이 10분동안 돌고있는 동안 server_status["signal"] 값이 바뀌어야 함.
        signal = client_status[tag_num]["signal"]
        print(elapsed_time)
        print(signal)
        if signal is not None:
            client_status[tag_num]["signal"] = None  # 신호 초기화
            return jsonify({"signal": signal}), 200

        time.sleep(interval)
        elapsed_time += interval

    return jsonify({"error": "Timeout waiting for signal"}), 408

@app.route('/set_signal/<int:tag_num>', methods=['POST'])
def set_signal(tag_num):
    """
    특정 클라이언트의 신호를 설정.
    """
    if tag_num not in client_status:
        return jsonify({"error": "Invalid tag_num"}), 400

    data = request.get_json()
    signal = data.get("signal")

    if signal not in ["on", "off"]:
        return jsonify({"error": "Invalid signal value, must be 'on' or 'off'"}), 400

    client_status[tag_num]["signal"] = signal
    client_status[tag_num]["timestamp"] = time.time()  # 업데이트 타임스탬프
    return jsonify({"message": f"Signal updated to \"{signal}\" for Sender with the tag_num: {tag_num}"}), 200

@app.route('/list_clients', methods=['GET'])
def list_clients():
    """
    연결된 모든 클라이언트 리스트 반환 (테스트용).
    """
    return jsonify(client_status), 200


# -----------------------------------------------------
# Handling User
# -----------------------------------------------------
@app.route("/upload", methods=["POST"])
def upload():
    try: 
        if "file" not in request.files:
                return jsonify({"error": "No file provided in the request."}), 400
        
        audio_file = request.files["file"]

        if audio_file.mimetype != "audio/mpeg":
            return jsonify({"error": "Invalid file type. Only MP3 files are allowed."}), 400
        
        if audio_file.filename == "":
            return jsonify({"error": "No selected file."}), 400
        
        dir_path = "./AudioFile"
        if not os.path.exists(dir_path):
            os.makedirs(dir_path)  # 디렉토리가 없으면 생성
        
        file_path = os.path.join(dir_path, audio_file.filename)
        audio_file.save(file_path)  # 파일 저장

        transcript = transcribe(file_path)

        # Recognizing Sender-side device
        tag_num = match_tag_num(transcript)
        print(f"Recognized tag number: {tag_num}")

        with app.test_request_context(f"/set_signal/{tag_num}", method="POST", json={"signal": "on"}):
            response = set_signal(tag_num)  # 직접 함수 호출

            response_data, status_code = response
            message = response_data.json.get("message")
            print(message)

        return Response(
            response=json.dumps({"output": transcript}, ensure_ascii=False), 
            status=200, 
            mimetype="application/json"
        )
        
    except Exception as e:
        return jsonify({"error": f"An error occurred: {str(e)}"}), 500


# -----------------------------------------------------
# Run main 
# -----------------------------------------------------
if __name__ == "__main__":
    # To deploy
    app.run()

    # To test
    #app.run('0.0.0.0', port=5001, debug=True)
    # app.run('0.0.0.0', port=5001, debug=False)