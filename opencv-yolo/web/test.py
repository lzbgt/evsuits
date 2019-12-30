import json, re

print(json.dumps( {
    "cameraId": "D72154040",
    "endTime": 1577267418999,
    "image": "http://evcloudsvc.ilabservice.cloud/video/D72154040/1550143347000-1577267418999/firstFrame.jpg",
    "length": 260,
    "startTime": 1550143347000,
    "video": "http://evcloudsvc.ilabservice.cloud/video/D72154040/1550143347000-1577267418999/1550143347000-1577267418999.mp4"
  }))


m = re.match(r".*? found (\w+) ([\d\.]+) .*? image: ([_\w\d]+.jpg)", "ObjectDetector found human 0.857669 x: 655, y: 379, w: 300, h: 309; written image: detect_person_1577696746490.jpg")

if m:
  print("matched", m.group(1))
else:
  print("no match")


