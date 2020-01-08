#!python
import argparse
import torch
from src.config import COCO_CLASSES, colors
import cv2, datetime
import numpy as np

def get_args():
    parser = argparse.ArgumentParser(
        "EfficientDet: Scalable and Efficient Object Detection implementation by Signatrix GmbH")
    parser.add_argument("--image_size", type=int, default=512, help="The common width and height for all images")
    parser.add_argument("--cls_threshold", type=float, default=0.5)
    parser.add_argument("--nms_threshold", type=float, default=0.5)
    parser.add_argument("-c", "--pretrained_model", type=str, default="edet_model.pth")
    parser.add_argument("input", type=str, default="input.mp4")
    parser.add_argument("-o", "--output", type=str, default="detect_person.jpg")
    args = parser.parse_args()
    return args

def test(opt):
    tsEpoch = datetime.datetime.utcfromtimestamp(0)
    model = torch.load(opt.pretrained_model, map_location='cpu').module
    if torch.cuda.is_available():
        model.cuda()

    cap = cv2.VideoCapture(opt.input)
    bDetected = False
    strDetMsg = ''
    ts = int((datetime.datetime.now() - tsEpoch).total_seconds())
    fname = opt.output + "_" + str(ts) + ".jpg"
    frameCnt = 0
    tsStart = datetime.datetime.now()
    while cap.isOpened():
        flag, image = cap.read()
        output_image = np.copy(image)
        if flag:
            image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        else:
            break
        frameCnt += 1
        if frameCnt % (18*3) == 0:
          pass
        else:
          continue
        height, width = image.shape[:2]
        image = image.astype(np.float32) / 255
        image[:, :, 0] = (image[:, :, 0] - 0.485) / 0.229
        image[:, :, 1] = (image[:, :, 1] - 0.456) / 0.224
        image[:, :, 2] = (image[:, :, 2] - 0.406) / 0.225
        if height > width:
            scale = opt.image_size / height
            resized_height = opt.image_size
            resized_width = int(width * scale)
        else:
            scale = opt.image_size / width
            resized_height = int(height * scale)
            resized_width = opt.image_size

        image = cv2.resize(image, (resized_width, resized_height))

        new_image = np.zeros((opt.image_size, opt.image_size, 3))
        new_image[0:resized_height, 0:resized_width] = image
        new_image = np.transpose(new_image, (2, 0, 1))
        new_image = new_image[None, :, :, :]
        new_image = torch.Tensor(new_image)
        if torch.cuda.is_available():
            new_image = new_image.cuda()
        with torch.no_grad():
            scores, labels, boxes = model(new_image)
            boxes /= scale
        if boxes.shape[0] == 0:
            continue

        for box_id in range(boxes.shape[0]):
            pred_prob = float(scores[box_id])
            if pred_prob < opt.cls_threshold:
                continue
            pred_label = int(labels[box_id])
            if COCO_CLASSES[pred_label] != 'person':
                continue
            xmin, ymin, xmax, ymax = boxes[box_id, :]
            color = colors[pred_label]
            color = (255,0,0)
            font = cv2.FONT_HERSHEY_PLAIN
            cv2.rectangle(output_image, (xmin, ymin), (xmax, ymax), color, 1)
            text_size = cv2.getTextSize(COCO_CLASSES[pred_label] + ' : %.2f' % pred_prob, font, 1, 1)[0]
            cv2.rectangle(output_image, (xmin, ymin), (xmin + text_size[0] + 1, ymin + text_size[1] + 1), color, -1)
            cv2.putText(
                output_image, COCO_CLASSES[pred_label] + ': %.3f' % pred_prob,
                (xmin, ymin + text_size[1] + 1), font, 1,
                (255, 255, 255), 1)
            if not bDetected:
              strDetMsg = "edet found human {:.3f} x: {}, y: {}, w: {}, h: {}; written image: {}".format(pred_prob, int(xmin), int(ymin), int(xmax-xmin), int(ymax-ymin), fname)
              bDetected = True

        if frameCnt % 1000 == 0:
            tsEnd = datetime.datetime.now()
            fps = frameCnt/(tsEnd - tsStart).total_seconds()
            print("fps: ", fps)
            frameCnt = 0
            tsStart = datetime.datetime.now()
        if bDetected:
            cv2.imwrite(fname, output_image)
            print(strDetMsg)
            break
    cap.release()

if __name__ == "__main__":
    opt = get_args()
    opt.output = opt.output[0:opt.output.rfind(".")]
    test(opt)
