import json
import boto3
from datetime import datetime
from decimal import Decimal  # Import Decimal type


def lambda_handler(event, context):
    print("Received event: " + json.dumps(event))  # This will show the complete event structure
    try:
        alcohol_mg_per_cubic = Decimal(str(event['alcohol_mg_per_cubic']))  # Convert to Decimal
        status = event['status']
        servo_angle = int(event['servo_angle'])
        location_data = event['location']

        # Get current UTC timestamp in ISO 8601 format
        timestamp = datetime.utcnow().isoformat() + 'Z'  # Appends 'Z' to indicate UTC time
        date = timestamp.split('T')[0]

        # Decide on sending alert based on alcohol level
        if alcohol_mg_per_cubic > Decimal('20.0'):  # Convert threshold to Decimal
            send_alert(status, location_data)

        # Log data to DynamoDB
        store_data_in_dynamodb(date, timestamp, alcohol_mg_per_cubic, status, servo_angle, location_data)
    except KeyError as e:
        print(f"Key error: {str(e)} - Check the incoming event structure.")
        return {
            'statusCode': 400,
            'body': json.dumps("Key error: Missing expected keys")
        }
    except Exception as e:
        print(f"Error: {str(e)}")
        return {
            'statusCode': 500,
            'body': json.dumps("Internal server error")
        }

    return {
        'statusCode': 200,
        'body': json.dumps('Data processed successfully')
    }


def send_alert(status, location):
    sns = boto3.client('sns')
    # Make sure to replace 'your-region' and '123456789012' with your actual AWS region and account ID
    topic_arn = 'arn:aws:sns:ap-southeast-1:905418438116:HighAlcoholLevelAlerts'
    message = f"Alert: {status} at location {location}"
    sns.publish(TopicArn=topic_arn, Message=message)


def store_data_in_dynamodb(date, timestamp, alcohol_level, status, angle, location):
    dynamodb = boto3.resource('dynamodb')
    table = dynamodb.Table('AlcoholData')
    response = table.put_item(
        Item={
            'Date': date,
            'Timestamp': timestamp,
            'AlcoholLevel': alcohol_level,
            'Status': status,
            'ServoAngle': angle,
            'Location': location
        }
    )
    return response
