#!/usr/bin/env python3
"""Reproducible Chronon3D 20-case rendered showcase suite."""
from __future__ import annotations
import argparse, hashlib, json, math, pathlib, resource, shutil, subprocess, sys, time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
MANIFEST_PATH = HERE / "manifest.json"
CLI_DEFAULT = REPO / "Chronon3d/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli"
PALETTE = ["#42D9FF", "#FF6B8A", "#A98BFF", "#FFD166", "#52E0A0", "#F4F7FF"]


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def rgb(color):
    return [int(color[i:i+2], 16) / 255 for i in (1, 3, 5)] + [1.0]


def run(command, cwd=REPO, log=None):
    completed = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, check=False)
    if log:
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(completed.stdout, encoding="utf-8")
    if completed.returncode:
        raise RuntimeError(f"exit {completed.returncode}: {' '.join(map(str, command))}\n{completed.stdout[-3000:]}")
    return completed.stdout


def scene(case, output):
    width, height, fps = 1920, 1080, 30
    frames = case["duration_seconds"] * fps
    slug, ident = case["slug"], case["id"]
    layers = [{"id": "background", "type": "shape", "size": [width, height],
               "position": [width/2, height/2], "start_frame": 0, "duration_frames": frames,
               "shape": {"type": "rect", "fill": [8/255, 13/255, 24/255, 1]}}]

    def shape(name, x, y, color, size=(100, 100), rotation=0, tracks=None, shape_type="ellipse"):
        item = {"id": name, "type": "shape", "size": list(size), "position": [x, y],
                "rotation": [0, 0, rotation], "enable_3d": True, "start_frame": 0, "duration_frames": frames,
                "shape": {"type": shape_type, "fill": rgb(color)}}
        if tracks:
            item["animation"] = {"tracks": tracks}
        return item

    def text(name, label, x, y, size, color):
        return {"id": name, "type": "text", "size": [1500, size*2], "position": [x,y],
                "start_frame": 0, "duration_frames": frames, "text": label,
                "style": {"font": "assets/fonts/Inter-Bold.ttf", "font_size": size, "fill": color}}

    layers.extend([text("title", f"{ident}  {case['name'].upper()}", 110, 125, 48, "#F4F7FF"),
                   text("scope", "CHRONON3D  /  SYNTHETIC INTEGRATION FIXTURE", 110, 190, 22, "#8292AE")])
    for i in range(8):
        x, y = 220 + (i % 4)*490, 440 + (i // 4)*260
        begin = int(i*frames/16)
        finish = min(frames-1, begin+max(2, fps//2))
        tracks = [{"property":"position", "easing":"in_out_cubic", "keyframes":[
            {"frame":begin,"value":[x-80,y]}, {"frame":finish,"value":[x,y]}]},
                  {"property":"opacity", "easing":"out_cubic", "keyframes":[
            {"frame":begin,"value":0.15}, {"frame":finish,"value":1.0}]}]
        layers.append(shape(f"accent_{i:02d}", x, y, PALETTE[i%6], (28+(i%4)*8,)*2, i*11, tracks))

    if slug in {"glowing_path_follow", "procedural_light_trails", "documentary_map_flyover"}:
        count = 1 if slug == "glowing_path_follow" else 5
        for i in range(count):
            offset = (i-(count-1)/2)*65
            layers.append({"id":f"route_{i}", "type":"shape", "size":[width,height],
                "position":[width/2,height/2], "enable_3d":True, "start_frame":0, "duration_frames":frames,
                "shape":{"type":"path", "path":[
                    {"type":"move_to","point":[-780,offset+180]},
                    {"type":"cubic_to","control1":[-260,offset-320],"control2":[260,offset+320],"point":[780,offset-180]}],
                    "stroke":{"color":PALETTE[i%6],"width":9,"dash_array":[2300,2300],"dash_offset":2300}},
                "animation":{"tracks":[{"property":"stroke_dash_offset","easing":"in_out_cubic","keyframes":[
                    {"frame":0,"value":2300},{"frame":frames-1,"value":0}]}]}})
    if slug == "kinetic_typography":
        for i, word in enumerate(["MAKE","IDEAS","MOVE","WITH","CHRONON"]):
            layers.append(text(f"word_{i}",word,250+(i%2)*760,390+(i//2)*115,72,PALETTE[i]))
    if slug == "cinematic_parallax_scene":
        for i in range(6):
            layers.append(shape(f"depth_layer_{i}",300+270*i,790-70*i,PALETTE[i],(230-i*16,130-i*8),i*3))
    if slug == "particle_vortex":
        layers.append({"id":"seeded_particles","type":"particles","size":[width,height],"position":[width/2,height/2],
            "duration_frames":frames,"particle_emitter":{"count":120,"seed":20261012,"emitter_size":[550,550],
            "velocity_min":[-4,-4],"velocity_max":[4,4],"lifetime":[2,7],"size":[2,12],"gravity":0,"turbulence":0.2,
            "color_ramp":[{"position":0,"color":[0.25,0.7,1,1]},{"position":1,"color":[0.65,0.25,1,0]}]}})
    for i in range(3):
        layers.append(shape(f"bottom_rule_{i}",110+i*45,1010,PALETTE[i],(28,8)))
    return {"schema":"chronon.render-plan.v3","version":3,"job_id":f"showcase_{ident}_{slug}",
            "canvas":{"width":width,"height":height,"fps_num":fps,"fps_den":1,"duration_frames":frames},
            "layers":layers,"output":{"path":str(output),"format":"mp4","codec":"h264"}}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli",type=pathlib.Path,default=CLI_DEFAULT)
    parser.add_argument("--output-root",type=pathlib.Path,default=HERE/"artifacts")
    parser.add_argument("--case",action="append")
    parser.add_argument("--list",action="store_true")
    parser.add_argument("--validate-only",action="store_true")
    parser.add_argument("--render-supported",action="store_true")
    parser.add_argument("--render-partial",action="store_true")
    parser.add_argument("--repeat",action="store_true")
    parser.add_argument("--backend",choices=["software","vulkan"],default="software")
    args=parser.parse_args()
    manifest=json.loads(MANIFEST_PATH.read_text(encoding="utf-8")); cases=manifest["cases"]
    if args.list:
        for c in cases: print(f"{c['id']} {c['slug']}: {c['duration_seconds']}s {c['status']} — {c['name']}")
        return 0
    cli=args.cli.resolve()
    if not cli.is_file(): parser.error(f"Chronon3D CLI not found: {cli}; pass --cli")
    selected=cases
    if args.case:
        selected=[c for c in cases if c["id"] in args.case or c["slug"] in args.case]
        found={v for c in selected for v in (c["id"],c["slug"])}
        if set(args.case)-found: parser.error("unknown cases: "+", ".join(sorted(set(args.case)-found)))
    if args.render_supported: selected=[c for c in selected if c["status"]!="BLOCKED"]
    elif not args.validate_only and not args.render_partial: parser.error("choose --validate-only, --render-supported, or --render-partial")
    root=args.output_root.resolve(); root.mkdir(parents=True,exist_ok=True); logs=root/"logs"
    report={"suite_id":manifest["suite_id"],"manifest_sha256":digest(MANIFEST_PATH),"renderer":str(cli),
        "backend":args.backend,"environment":{"python":sys.version.split()[0],"platform":sys.platform,
        "ffmpeg":shutil.which("ffmpeg"),"ffprobe":shutil.which("ffprobe")},"cases":[]}
    for case in selected:
        folder=root/f"{case['id']}_{case['slug']}"; folder.mkdir(parents=True,exist_ok=True)
        n=case["duration_seconds"]*30; video=folder/"output.mp4"; plan=folder/"scene.json"
        plan.write_text(json.dumps(scene(case,video),indent=2)+"\n",encoding="utf-8")
        expected={"case_id":case["id"],"slug":case["slug"],"duration_seconds":case["duration_seconds"],
            "width":1920,"height":1080,"fps_num":30,"fps_den":1,"frame_count":n,
            "requested_status":case["status"],"implemented":case["implemented"],"blocked_features":case["blocked"]}
        (folder/"expected.json").write_text(json.dumps(expected,indent=2)+"\n",encoding="utf-8")
        result={**expected,"scene_sha256":digest(plan),"status":"BLOCKED" if case["status"]=="BLOCKED" else "PARTIAL",
            "technical_status":"NOT_RUN","functional_pass":False,"artifacts":{"scene":str(plan),"expected":str(folder/"expected.json")}}
        started=time.monotonic()
        try:
            vlog=logs/f"{case['id']}_validate.log"
            validation=run([str(cli),"validate","--plan",str(plan),"--assets-root",str(REPO/"Chronon3d")],log=vlog)
            json_start=validation.rfind("\n{")
            val=json.loads(validation[json_start+1:] if json_start >= 0 else validation[validation.find("{"):])
            result["artifacts"]["validation_log"]=str(vlog)
            if val.get("status")!="PASS" or val.get("valid") is not True: raise RuntimeError(f"plan validation failed: {val}")
            expected["validated_plan_sha256"]=digest(plan)
            (folder/"expected.json").write_text(json.dumps(expected,indent=2)+"\n",encoding="utf-8")
            result.update(expected)
            if not args.validate_only and case["status"]!="BLOCKED":
                log=logs/f"{case['id']}_render.log"
                run([str(cli),"render","--plan",str(plan),"--backend",args.backend,"--profile","preview",
                     "--hardware","none","--fps","30","--encoder-backend","native","--assets-root",str(REPO/"Chronon3d"),"-o",str(video)],log=log)
                probe_out=run(["ffprobe","-v","error","-select_streams","v:0","-show_entries",
                    "stream=codec_name,width,height,pix_fmt,r_frame_rate,nb_frames,duration,time_base",
                    "-show_entries","format=duration","-of","json",str(video)])
                probe=json.loads(probe_out); stream=probe["streams"][0]
                if (stream.get("codec_name")!="h264" or int(stream["width"])!=1920 or int(stream["height"])!=1080
                    or stream["r_frame_rate"]!="30/1" or int(stream.get("nb_frames",-1))!=n):
                    raise RuntimeError(f"stream contract mismatch: {stream}")
                frames_dir=folder/"frames"; frames_dir.mkdir(exist_ok=True)
                hashes_out=run(["ffmpeg","-v","error","-i",str(video),"-f","framemd5","-"])
                hashes=[line.rsplit(",",1)[-1].strip() for line in hashes_out.splitlines() if line.strip() and not line.startswith("#")]
                if len(hashes)!=n or len(set(hashes))<2: raise RuntimeError(f"decoded frame count/change check failed: {len(hashes)}")
                if any(hashes[index] == hashes[index+1] for index in range(len(hashes)-1)):
                    # Repeated hashes are evidence, not automatic failure: sparse rendered changes may legitimately hold frames.
                    pass
                for idx in sorted(set((0,n//2,n-1))):
                    target=frames_dir/f"frame_{idx:03}.png"
                    run(["ffmpeg","-v","error","-y","-i",str(video),"-vf",f"select=eq(n\\,{idx})","-vsync","0","-frames:v","1",str(target)])
                    if not target.is_file() or target.stat().st_size==0: raise RuntimeError(f"missing frame {idx}")
                repeated=None
                if args.repeat:
                    repeat=folder/"repeat.mp4"
                    run([str(cli),"render","--plan",str(plan),"--backend",args.backend,"--profile","preview",
                         "--hardware","none","--fps","30","--encoder-backend","native","--assets-root",str(REPO/"Chronon3d"),"-o",str(repeat)],log=logs/f"{case['id']}_repeat.log")
                    rh=run(["ffmpeg","-v","error","-i",str(repeat),"-f","framemd5","-"])
                    rh=[line.rsplit(",",1)[-1].strip() for line in rh.splitlines() if line.strip() and not line.startswith("#")]
                    repeated=hashes==rh
                timing_path=pathlib.Path(str(video)+".timing.json")
                timing=json.loads(timing_path.read_text()) if timing_path.is_file() else {}
                def finite_numbers(value):
                    if isinstance(value, dict):
                        for child in value.values(): yield from finite_numbers(child)
                    elif isinstance(value, list):
                        for child in value: yield from finite_numbers(child)
                    elif isinstance(value, (int, float)) and not isinstance(value, bool):
                        yield float(value)
                nums=list(finite_numbers(timing))
                if any(not math.isfinite(value) for value in nums): raise RuntimeError("non-finite timing metric")
                if int(probe["streams"][0].get("nb_frames", -1)) != n or abs(float(probe["format"]["duration"])-case["duration_seconds"]) > 1/30000:
                    raise RuntimeError(f"exact duration/frame contract failed: {probe}")
                frame_coverage=[]
                for idx in sorted(set((0,n//2,n-1))):
                    target=frames_dir/f"frame_{idx:03}.png"
                    if not target.is_file() or target.stat().st_size==0: raise RuntimeError(f"missing frame {idx}")
                    stats=run(["ffmpeg","-v","info","-i",str(target),"-vf","signalstats,metadata=print","-frames:v","1","-f","null","-"])
                    import re
                    values={key:float(value) for key,value in re.findall(r"lavfi\.signalstats\.(YAVG|UAVG|VAVG|YMAX|YMIN)=([0-9.]+)",stats)}
                    if not {"YAVG","UAVG","VAVG"}.issubset(values) or values["YAVG"] <= 0:
                        raise RuntimeError(f"representative frame has no finite measurable pixel coverage: {idx}: {values}")
                    frame_coverage.append({"frame":idx,"bytes":target.stat().st_size,"signalstats":values})
                timing_ms=timing.get("frame_times_ms", [])
                if not isinstance(timing_ms, list) or len(timing_ms) != n:
                    raise RuntimeError(f"timing sidecar has no exact per-frame samples: {len(timing_ms) if isinstance(timing_ms,list) else 'missing'} vs {n}")
                result["representative_frames"]=frame_coverage
                result["timing_values_checked"]=len(nums)
                result.update({"status":"PARTIAL" if case["status"]!="PASS" else "PASS",
                    "technical_status":"PASS","functional_pass":False,"scope_note":"Video is a deterministic diagnostic fixture, not a visual golden or full functional certification.","probe":stream,
                    "decoded_frame_sha256":hashlib.sha256("\n".join(hashes).encode()).hexdigest(),
                    "distinct_decoded_frames":len(set(hashes)),"render_wall_seconds":round(time.monotonic()-started,6),
                    "peak_rss_kib_process":resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,
                    "repeat_decoded_hashes_equal":repeated,"artifacts":{**result["artifacts"],"video":str(video),"metrics":str(folder/"metrics.json"),
                    "timing":str(timing_path),"frames_dir":str(frames_dir),"probe":str(folder/"probe.json"),"render_log":str(log)}})
                (folder/"probe.json").write_text(json.dumps(probe,indent=2)+"\n")
                result["expected_sha256"]=digest(folder/"expected.json")
                metric_fields=("case_id","slug","scene_sha256","probe","decoded_frame_sha256","distinct_decoded_frames","render_wall_seconds","peak_rss_kib_process","timing_values_checked","representative_frames","repeat_decoded_hashes_equal","technical_status","functional_pass","scope_note")
                (folder/"metrics.json").write_text(json.dumps({key:result.get(key) for key in metric_fields},indent=2)+"\n",encoding="utf-8")
            result["artifacts"]["metrics"]=str(folder/"metrics.json")
            (folder/"expected.json").write_text(json.dumps(expected,indent=2)+"\n",encoding="utf-8")
            result["expected_sha256"]=digest(folder/"expected.json")
        except Exception as exc:
            result.update({"status":"FAIL","technical_status":"FAIL","error":str(exc)})
        if result["technical_status"] == "NOT_RUN":
            result["functional_status"] = "BLOCKED" if case["status"] == "BLOCKED" else "NOT_CERTIFIED"
        report["cases"].append(result); print(f"{result['status']:7} {case['id']} {case['slug']}: {result['technical_status']}")
    report["summary"]={s:sum(c["status"]==s for c in report["cases"]) for s in ("PASS","PARTIAL","BLOCKED","FAIL")}
    report_path=root/"report.json"; report_path.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(f"report: {report_path} {report['summary']}")
    return int(report["summary"]["FAIL"]>0)

if __name__=="__main__": raise SystemExit(main())
