import uuid
import time
from azure.storage.blob import BlobServiceClient, BlobClient

# CONFIGURATION
connection_string = "<Connection-String>"
command_container = "configuration"     # container holding telemetry.conf
response_container = "stream"     # container where agents upload results
telemetry_blob_name = "telemetry.conf"       # blob file holding uuid,command

# Initialize Azure Blob client
blob_service = BlobServiceClient.from_connection_string(connection_string)

def write_single_command(uuid_str, command):
    blob_client = blob_service.get_blob_client(container=command_container, blob=telemetry_blob_name)
    line = f"{uuid_str},{command}\n"
    blob_client.upload_blob(line, overwrite=True)
    print(f"[INFO] Sent command with UUID: {uuid_str}")

def wait_for_response(uuid_str):
    container_client = blob_service.get_container_client(response_container)
    print("[INFO] Waiting for agent response...")

    timeout_seconds = 120
    poll_interval = 2
    elapsed = 0

    while elapsed < timeout_seconds:
        blob_list = container_client.list_blobs(name_starts_with=uuid_str + ".")
        matching_blobs = list(blob_list)
        if matching_blobs:
            blob_name = matching_blobs[0].name
            blob_client = container_client.get_blob_client(blob_name)
            content = blob_client.download_blob().readall().decode()
            print(f"\n=== Response from {blob_name} ===")
            print(content)
            print("=" * 40 + "\n")
            return
        time.sleep(poll_interval)
        elapsed += poll_interval

    print("[ERROR] Timed out waiting for response.\n")

def main():
    print("Telemetry Command Server started. Type commands below (Ctrl+C to exit).")
    while True:
        try:
            user_command = input(">>> ").strip()
            if not user_command:
                continue
            new_uuid = str(uuid.uuid4())
            write_single_command(new_uuid, user_command)
            wait_for_response(new_uuid)
        except KeyboardInterrupt:
            print("\n[INFO] Exiting.")
            break
        except Exception as e:
            print(f"[ERROR] {e}\n")

if __name__ == "__main__":
    main()