import json
import math
import copy
from pathlib import Path

template_path = Path("Chronon3d/docs/evidence/camera-closure-final/optical_current_pair_v3.plan.json")
with open(template_path, "r") as f:
    base_plan = json.load(f)

ASSETS_DIR = "docs/evidence/real_cinematic/assets"

def make_base_cinematic_plan(job_id, output_name):
    plan = copy.deepcopy(base_plan)
    plan["job_id"] = job_id
    plan["output"]["path"] = output_name
    
    # Layer 1: Background
    bg = copy.deepcopy(base_plan["layers"][0])
    bg["id"] = "1"
    bg["asset"] = f"{ASSETS_DIR}/background.png"
    bg["size"] = [3840.0, 2160.0]
    bg["opacity"] = 1.0
    bg["color"] = [1.0, 1.0, 1.0, 1.0]
    
    # Layer 2: Car (Midground)
    car = copy.deepcopy(base_plan["layers"][2])
    car["id"] = "2"
    car["asset"] = f"{ASSETS_DIR}/car.png"
    car["size"] = [850.0, 570.0]
    car["opacity"] = 1.0
    car["color"] = [1.0, 1.0, 1.0, 1.0]
    
    # Layer 3: Drone (Foreground)
    drone = copy.deepcopy(base_plan["layers"][3])
    drone["id"] = "3"
    drone["asset"] = f"{ASSETS_DIR}/drone.png"
    drone["size"] = [620.0, 355.0]
    drone["opacity"] = 1.0
    drone["color"] = [1.0, 1.0, 1.0, 1.0]
    
    plan["layers"] = [bg, car, drone]
    return plan

def smoothstep(edge0, edge1, x):
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)

# -------------------------------------------------------------
# Plan 1: Cinematic Rack Focus (Drone -> Car -> City Background)
# -------------------------------------------------------------
plan1 = make_base_cinematic_plan("cinematic_real_rack_focus", "cinematic_real_rack_focus.mp4")

plan1_cam_samples = []
bg_samples1 = []
car_samples1 = []
drone_samples1 = []

for s_orig in base_plan["camera_samples"]:
    t_frame = s_orig["frame"]
    t_norm = max(0.0, min(1.0, t_frame / 149.0))
    
    # Subtle horizontal camera slide for parallax
    cam_x = -80.0 + 160.0 * smoothstep(0.0, 1.0, t_norm)
    cam_y = 0.0
    cam_z = -1000.0
    
    # Rack focus calculation:
    # 0.0 - 0.28 (f 0..42): focus on drone (dist = 750)
    # 0.28 - 0.58 (f 42..86): transition to car (dist = 1000)
    # 0.58 - 0.90 (f 86..134): transition to city background (dist = 2200)
    # 0.90 - 1.0 (f 134..149): hold on background
    if t_norm < 0.28:
        f_dist = 750.0
    elif t_norm < 0.58:
        w = smoothstep(0.28, 0.58, t_norm)
        f_dist = 750.0 + (1000.0 - 750.0) * w
    elif t_norm < 0.90:
        w = smoothstep(0.58, 0.90, t_norm)
        f_dist = 1000.0 + (2200.0 - 1000.0) * w
    else:
        f_dist = 2200.0
        
    cam_sample = copy.deepcopy(s_orig)
    cam_sample["camera"]["position"] = [cam_x, cam_y, cam_z]
    cam_sample["camera"]["dof"]["focus_distance"] = f_dist
    cam_sample["camera"]["dof"]["max_blur"] = 14.0
    cam_sample["camera"]["f_stop"] = 1.8
    plan1_cam_samples.append(cam_sample)
    
    bg_samples1.append({
        "frame": t_frame,
        "position": [0.0, 0.0, 1200.0],
        "rotation_deg": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
        "opacity": 1.0
    })
    
    car_samples1.append({
        "frame": t_frame,
        "position": [140.0, 80.0, 0.0],
        "rotation_deg": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
        "opacity": 1.0
    })
    
    drone_hover_y = -10.0 + 10.0 * math.sin(t_frame * 0.12)
    drone_samples1.append({
        "frame": t_frame,
        "position": [-180.0, drone_hover_y, -250.0],
        "rotation_deg": [1.5 * math.sin(t_frame * 0.1), 2.5 * math.cos(t_frame * 0.08), 0.0],
        "scale": [1.0, 1.0, 1.0],
        "opacity": 1.0
    })

plan1["camera_samples"] = plan1_cam_samples
plan1["layers"][0]["transform_samples"] = bg_samples1
plan1["layers"][1]["transform_samples"] = car_samples1
plan1["layers"][2]["transform_samples"] = drone_samples1

with open("Chronon3d/docs/evidence/real_cinematic/cinematic_real_rack_focus.plan.json", "w") as f:
    json.dump(plan1, f, indent=2)

# Sharp Control version (DoF OFF)
plan1_sharp = copy.deepcopy(plan1)
plan1_sharp["job_id"] = "cinematic_real_rack_focus_sharp"
plan1_sharp["output"]["path"] = "cinematic_real_rack_focus_sharp.mp4"
for s in plan1_sharp["camera_samples"]:
    s["camera"]["dof"]["enabled"] = False
    s["camera"]["f_stop"] = 0.0
with open("Chronon3d/docs/evidence/real_cinematic/cinematic_real_rack_focus_sharp.plan.json", "w") as f:
    json.dump(plan1_sharp, f, indent=2)

# -------------------------------------------------------------
# Plan 2: Dynamic 3D Dolly-In / Push-In (Camera rushes toward Car)
# -------------------------------------------------------------
plan2 = make_base_cinematic_plan("cinematic_real_dolly_in", "cinematic_real_dolly_in.mp4")

plan2_cam_samples = []
bg_samples2 = []
car_samples2 = []
drone_samples2 = []

for s_orig in base_plan["camera_samples"]:
    t_frame = s_orig["frame"]
    t_norm = max(0.0, min(1.0, t_frame / 149.0))
    
    progress = smoothstep(0.0, 1.0, t_norm)
    cam_x = -50.0 + 100.0 * progress
    cam_y = 10.0 - 15.0 * progress
    cam_z = -1350.0 + 800.0 * progress  # Camera flies from -1350 in toward -550
    
    # Locked tracking focus on the car at Z=0
    f_dist = -cam_z
    
    cam_sample = copy.deepcopy(s_orig)
    cam_sample["camera"]["position"] = [cam_x, cam_y, cam_z]
    cam_sample["camera"]["dof"]["focus_distance"] = f_dist
    cam_sample["camera"]["dof"]["max_blur"] = 12.0
    cam_sample["camera"]["f_stop"] = 2.0
    plan2_cam_samples.append(cam_sample)
    
    bg_samples2.append({
        "frame": t_frame,
        "position": [0.0, 0.0, 1200.0],
        "rotation_deg": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
        "opacity": 1.0
    })
    
    car_samples2.append({
        "frame": t_frame,
        "position": [80.0, 70.0, 0.0],
        "rotation_deg": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
        "opacity": 1.0
    })
    
    drone_hover_y = -20.0 + 8.0 * math.sin(t_frame * 0.15)
    drone_samples2.append({
        "frame": t_frame,
        "position": [-240.0, drone_hover_y, -600.0],
        "rotation_deg": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
        "opacity": 1.0
    })

plan2["camera_samples"] = plan2_cam_samples
plan2["layers"][0]["transform_samples"] = bg_samples2
plan2["layers"][1]["transform_samples"] = car_samples2
plan2["layers"][2]["transform_samples"] = drone_samples2

with open("Chronon3d/docs/evidence/real_cinematic/cinematic_real_dolly_in.plan.json", "w") as f:
    json.dump(plan2, f, indent=2)

# -------------------------------------------------------------
# Plan 3: Orbital Arc / Sweeping Parallax
# -------------------------------------------------------------
plan3 = make_base_cinematic_plan("cinematic_real_orbital_arc", "cinematic_real_orbital_arc.mp4")

plan3_cam_samples = []
bg_samples3 = []
car_samples3 = []
drone_samples3 = []

for s_orig in base_plan["camera_samples"]:
    t_frame = s_orig["frame"]
    t_norm = max(0.0, min(1.0, t_frame / 149.0))
    
    angle_rad = (-16.0 + 32.0 * smoothstep(0.0, 1.0, t_norm)) * (math.pi / 180.0)
    radius = 1000.0
    cam_x = radius * math.sin(angle_rad)
    cam_z = -radius * math.cos(angle_rad)
    cam_y = -20.0 + 25.0 * math.sin(t_norm * math.pi)
    
    yaw_deg = -angle_rad * (180.0 / math.pi)
    f_dist = 920.0
    
    cam_sample = copy.deepcopy(s_orig)
    cam_sample["camera"]["position"] = [cam_x, cam_y, cam_z]
    cam_sample["camera"]["rotation_deg"] = [0.0, yaw_deg, 0.0]
    cam_sample["camera"]["dof"]["focus_distance"] = f_dist
    cam_sample["camera"]["dof"]["max_blur"] = 14.0
    cam_sample["camera"]["f_stop"] = 2.0
    plan3_cam_samples.append(cam_sample)
    
    bg_samples3.append({
        "frame": t_frame,
        "position": [0.0, 0.0, 1200.0],
        "rotation_deg": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
        "opacity": 1.0
    })
    
    car_samples3.append({
        "frame": t_frame,
        "position": [40.0, 70.0, 0.0],
        "rotation_deg": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
        "opacity": 1.0
    })
    
    drone_hover_y = -15.0 + 10.0 * math.sin(t_frame * 0.1)
    drone_samples3.append({
        "frame": t_frame,
        "position": [-170.0, drone_hover_y, -250.0],
        "rotation_deg": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
        "opacity": 1.0
    })

plan3["camera_samples"] = plan3_cam_samples
plan3["layers"][0]["transform_samples"] = bg_samples3
plan3["layers"][1]["transform_samples"] = car_samples3
plan3["layers"][2]["transform_samples"] = drone_samples3

with open("Chronon3d/docs/evidence/real_cinematic/cinematic_real_orbital_arc.plan.json", "w") as f:
    json.dump(plan3, f, indent=2)

print("Generated all 4 updated plans successfully.")
