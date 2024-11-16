import requests
import time

# 서버의 IP 주소와 포트
SERVER_URL = "http://0.0.0.0:5000"   # ip needed

# 서버 연결 요청
tag_num = 11  # 정수형 변수 (예: 11)
response = requests.post(f"{SERVER_URL}/connect", json={"tag_num": tag_num})

if response.status_code == 200:
    print("Connection established:", response.json())
else:
    print("Connection failed. Status code:", response.status_code)

# 서버로부터 신호 받기
try:
    print("Waiting for server signal...")

    response = requests.post(f"{SERVER_URL}/get_signal/{tag_num}", json={"tag_num": tag_num})

    print(response)
    
    if response.status_code == 200:
        signal = response.json().get("signal")
        # signal = response.json()
        print("Received signal:", response)
        print(signal)

        # 그 다음 처리
        if signal == "on":
            print("Performing ON signal actions...")
            # 재원 부분 
            # UWB 활성화
        elif signal == "off":
            # 
            print("Performing OFF signal actions...")
    else:
        print("Error:", response.status_code, response.json())

except requests.exceptions.Timeout:
    print("Request timed out while waiting for signal.")
