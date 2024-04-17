import json
import boto3

def lambda_handler(event, context):
    # Safely get the 'queryStringParameters' dictionary
    query_params = event.get('queryStringParameters', {})

    # Safely get the 'angle' parameter from the query parameters
    servo_angle = query_params.get('angle')
    if servo_angle is None:
        # If 'angle' is not provided, return an error message
        return {
            'statusCode': 400,
            'body': json.dumps("Missing 'angle' query parameter")
        }

    client = boto3.client('iot-data', region_name='ap-southeast-1')
    message = {'servo_angle': int(servo_angle)}

    # Publish to MQTT topic
    response = client.publish(
        topic='esp32/control',
        qos=1,
        payload=json.dumps(message)
    )

    return {
        'statusCode': 200,
        'body': json.dumps('Message sent to ESP32 successfully!')
    }
