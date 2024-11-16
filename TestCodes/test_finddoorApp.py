import requests

# 서버 URL
url = "http://0.0.0.0:5000/upload"  # ip needed

# 파일 경로 (전송할 오디오 파일)
p11="/Users/jaehpark/Desktop/2024-Fall-Makerthon/code/project/user-gistLibFrontDoor.mp3"
file_path = p11

file_name = file_path.split("/")[-1]

# 파일 열기
with open(file_path, "rb") as file:
    # 파일 데이터 준비
    files = {"file": (file_name, file, "audio/mpeg")}

    # POST 요청 보내기
    response = requests.post(url, files=files)

    

# 응답 출력
if response.status_code == 200:
    print("파일 업로드 성공!")
    print("응답 내용:", response.text)
else:
    print("파일 업로드 실패:", response.status_code)
    print("응답 내용:", response.text)
