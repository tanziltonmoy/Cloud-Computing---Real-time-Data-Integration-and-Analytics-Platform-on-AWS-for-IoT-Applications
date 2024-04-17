import json
import boto3
import time
import uuid

def lambda_handler(event, context):
    # Initialize AWS IoT SiteWise client
    sitewise = boto3.client('iotsitewise')

    # Check if the event is from API Gateway with servo angle command
    servo_angle_command = event.get('queryStringParameters', {}).get('servo_angle')
    if servo_angle_command:
        servo_angle = int(servo_angle_command)
    else:
        # Extract data from the normal payload if not a specific command
        servo_angle = event.get('servo_angle', 0)  # Default to 0 if not specified
        alcohol_mg_per_cubic = event.get('alcohol_mg_per_cubic', 0)
        status = event.get('status', 'Normal')
        location_data = json.loads(event.get('location', '{}'))

    # Define your asset and property IDs
    asset_id = "0f0c8e26-874e-49ec-8235-be5f3261be78"
    property_ids = {
        "alcohol_level": "bc93cc4c-f162-4ccd-a0da-9ac4f2374ba2",
        "location_data": "156fe7f9-a407-46d2-976c-c519d594d9ea",
        "servo_angle": "8e7f479c-09e4-4e44-bb69-481b41a78137",
        "status": "17fe56a5-971b-4ea9-9be3-ddc66cb869c2"
    }

    # Prepare the entries list for batch update
    entries = [
        {
            'entryId': str(uuid.uuid4()),
            'assetId': asset_id,
            'propertyId': property_ids['servo_angle'],
            'propertyValues': [{'value': {'integerValue': servo_angle}, 'timestamp': {'timeInSeconds': int(time.time()), 'offsetInNanos': 0}}]
        }
    ]

    # Add additional property updates only if not a specific command
    if not servo_angle_command:
        entries.extend([
            {
                'entryId': str(uuid.uuid4()),
                'assetId': asset_id,
                'propertyId': property_ids['alcohol_level'],
                'propertyValues': [{'value': {'doubleValue': alcohol_mg_per_cubic}, 'timestamp': {'timeInSeconds': int(time.time()), 'offsetInNanos': 0}}]
            },
            {
                'entryId': str(uuid.uuid4()),
                'assetId': asset_id,
                'propertyId': property_ids['status'],
                'propertyValues': [{'value': {'stringValue': status}, 'timestamp': {'timeInSeconds': int(time.time()), 'offsetInNanos': 0}}]
            },
            {
                'entryId': str(uuid.uuid4()),
                'assetId': asset_id,
                'propertyId': property_ids['location_data'],
                'propertyValues': [{'value': {'stringValue': json.dumps(location_data)}, 'timestamp': {'timeInSeconds': int(time.time()), 'offsetInNanos': 0}}]
            }
        ])

    # Update properties in AWS IoT SiteWise using a batch update
    response = sitewise.batch_put_asset_property_value(entries=entries)

    return {
        'statusCode': 200,
        'body': json.dumps("Successfully updated IoT SiteWise asset properties"),
        'response': response
    }