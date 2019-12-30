import paho.mqtt.client as mqtt

class VAMMQTTClient:
  # The callback for when the client receives a CONNACK response from the server.
  @staticmethod
  def on_connect(client, userdata, flags, rc):
      print("Connected with result code "+str(rc))

      # Subscribing in on_connect() means that if we lose the connection and
      # reconnect then subscriptions will be renewed.
      client.subscribe("$queue/video.ai/v1.0/task")

  # The callback for when a PUBLISH message is received from the server.
  @staticmethod
  def on_message(client, userdata, msg):
      print(msg.topic+" "+str(msg.payload))
      userdata(msg)

  def __init__(self, callback, host='evcloud.ilabservice.cloud', port = 1883):
    '''
    Parameters
    '''
    self.client = mqtt.Client(userdata=callback)
    self.client.on_connect = self.on_connect
    self.on_message = self.on_message

    self.client.connect(host, port, 30)
    self.client.loop_start()