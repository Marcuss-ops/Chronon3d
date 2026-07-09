import subprocess
import os
import time

env = os.environ.copy()
env["CHRONON3D_DASHBOARD_PASSWORD"] = "dummy"
# Assuming standard backend port is 8000
env["PORT"] = "8000"

# Based on file structure, it seems the backend is in tools/telemetry_dashboard/backend/app.py
# Or it might be the server.py mentioned earlier.
# Let's try to run the file that has the /api/runs route.
proc = subprocess.Popen(["python3", "tools/telemetry_dashboard/backend/app.py"], env=env)
try:
    for i in range(10):
        time.sleep(1)
        print(f"Waiting for backend: {i}")
except KeyboardInterrupt:
    proc.terminate()
