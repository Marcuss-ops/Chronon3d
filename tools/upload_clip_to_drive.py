import sys
import os
import json
from google.oauth2.credentials import Credentials
from googleapiclient.discovery import build
from googleapiclient.http import MediaFileUpload

FOLDER_ID = '1ST6FxPuRaxwBOIz39MAN8Jj4gDv509-K'
CREDS_FILE = '/home/pierone/src/go-master/projects/Pyt/VeloxEditing/refactored/credentials.json'
TOKEN_FILE = '/home/pierone/src/go-master/projects/Pyt/VeloxEditing/refactored/token.json'
FILE_PATH = sys.argv[1] if len(sys.argv) > 1 else '/tmp/matt_damon_1080p_gpu_native.mp4'

if not os.path.exists(FILE_PATH):
    print(f"Error: {FILE_PATH} does not exist", file=sys.stderr)
    sys.exit(1)

with open(CREDS_FILE) as f:
    creds_data = json.load(f)
with open(TOKEN_FILE) as f:
    token_data = json.load(f)

cfg = creds_data.get('installed') or creds_data.get('web')
creds = Credentials(
    token=token_data.get('access_token'),
    refresh_token=token_data.get('refresh_token'),
    token_uri=cfg.get('token_uri', 'https://oauth2.googleapis.com/token'),
    client_id=cfg.get('client_id'),
    client_secret=cfg.get('client_secret'),
    scopes=['https://www.googleapis.com/auth/drive']
)

service = build('drive', 'v3', credentials=creds)
filename = os.path.basename(FILE_PATH)

media = MediaFileUpload(FILE_PATH, mimetype='video/mp4', resumable=True)
file_metadata = {
    'name': filename,
    'parents': [FOLDER_ID]
}

print(f"Uploading {filename} ({os.path.getsize(FILE_PATH)} bytes) to Drive folder {FOLDER_ID}...")
uploaded = service.files().create(body=file_metadata, media_body=media, fields='id, name, webViewLink').execute()
print(f"Upload complete! File ID: {uploaded.get('id')} Link: {uploaded.get('webViewLink')}")
