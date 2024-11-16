import requests

place_dict = {"도서관 정문" : 11, # tag_name : tag_num
              "도서관 후문" : 22,
              "기숙사 정문" : 33,
              "기숙사 후문" : 44,
              "학생회관 정문" : 55,
              "학생회관 후문" : 66}

def match_tag_num(text_msg):
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

# 어플로부터 오디오 파일 받기 

## 오디오 파일 받은 과정 대체:
audio_file = "./project/user-gistLibFrontDoor.mp3"

# 위스퍼 모델 불러와서 오디오 파일을 텍스트로 변환하기 

## <After STT>
## 위스퍼 모델이 STT 과정 대체:
transcript = "지스트 도서관 정문 찾아줘"

tag_num = match_tag_num(transcript)
print(f"Recognized tag number: {tag_num}")

url = f"http://0.0.0.0:5000/set_signal/{tag_num}"   # ip needed
headers = {"Content-Type": "application/json"}
payload = {"signal": "on"}

response = requests.post(url, json=payload, headers=headers)
print(response.json())