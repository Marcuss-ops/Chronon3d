#!/usr/bin/env python3
"""Generate and verify six documentary Glow Lines 2.0 RenderPlan V3 clips."""
from __future__ import annotations
import argparse, hashlib, json, math, pathlib, shutil, subprocess, sys, time
from PIL import Image, ImageDraw
import numpy as np
from scipy.ndimage import distance_transform_edt

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
CLI_DEFAULT = REPO / "Chronon3d/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli"
OUTPUT_DEFAULT = (REPO / "../out/glow_lines_v2").resolve()
W, H, FPS = 1920, 1080, 30
BG = "#17191B"
IVORY, AMBER, RED, STEEL, WARM = "#DDD8CE", "#C59A63", "#B65F55", "#91A9B5", "#99958D"

CASES = [
    {"id":"01", "slug":"01_fine_stroke", "name":"Fine editorial stroke", "duration":5,
     "purpose":"Subpixel hairline stability", "color":IVORY, "width":1.4, "glow":None, "draw":False, "background_asset":"assets/editorial/industrial_port_dawn.png"},
    {"id":"02", "slug":"02_soft_glow", "name":"Restrained soft glow", "duration":5,
     "purpose":"Sharp core with measured halo", "color":IVORY, "width":1.6, "glow":{"radius":5.0,"intensity":0.12,"hex_color":AMBER}, "draw":False, "background_asset":"assets/editorial/industrial_port_dawn.png"},
    {"id":"03", "slug":"03_path_reveal", "name":"Arc-length path reveal", "duration":6,
     "purpose":"Uniform reveal along a cubic route", "color":AMBER, "width":1.8, "glow":{"radius":4.0,"intensity":0.10,"hex_color":AMBER}, "draw":True, "background_asset":"assets/maps/alps_basemap_doc.png"},
    {"id":"04", "slug":"04_light_decay", "name":"Light decay", "duration":5,
     "purpose":"Transient draw energy then quiet readable rest state", "color":IVORY, "width":1.6, "glow":{"radius":5.0,"intensity":0.30,"hex_color":AMBER}, "draw":True, "decay":True, "background_asset":"assets/editorial/industrial_port_dawn.png"},
    {"id":"05", "slug":"05_documentary_map", "name":"Documentary map route", "duration":7,
     "purpose":"Amber route over provided archival-style map plate", "color":AMBER, "width":1.8, "glow":{"radius":5.0,"intensity":0.13,"hex_color":AMBER}, "draw":True, "asset":"assets/maps/alps_basemap_doc.png"},
    {"id":"06", "slug":"06_evidence_connections", "name":"Evidence connections", "duration":8,
     "purpose":"Muted red links between archive image and field note", "color":RED, "width":1.5, "glow":{"radius":3.0,"intensity":0.08,"hex_color":RED}, "draw":True, "evidence":True, "background_asset":"assets/editorial/industrial_port_dawn.png"},
]


def digest(path: pathlib.Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''): h.update(chunk)
    return h.hexdigest()


def decay_halo_reduction(signals, minimum_reduction=0.12):
    if len(signals) != 3:
        raise ValueError('decay measurement requires baseline/pre/post halo signals')
    baseline, before, after = signals
    if not all(isinstance(value, (int, float)) and math.isfinite(value) for value in signals):
        raise ValueError(f'invalid baseline/pre/post halo signals: {signals}')
    pre_excess, post_excess = before - baseline, after - baseline
    if pre_excess <= 0:
        raise ValueError(f'pre-decay halo excess is not measurable above static plate: {pre_excess}')
    reduction = 1.0 - post_excess / pre_excess
    if reduction < minimum_reduction:
        raise ValueError(f'baseline-corrected halo reduction {reduction:.3%} is below required {minimum_reduction:.3%}')
    return {'baseline_halo_signal':baseline,'pre_halo_signal':before,'post_halo_signal':after,
            'pre_halo_excess':pre_excess,'post_halo_excess':post_excess,'relative_reduction':reduction}


def route_halo_roi(plan, case):
    route=next(layer for layer in plan['layers'] if layer.get('id')=='route_0')
    cx,cy=route['position']; path=route['shape']['path']
    p0=path[0]['point']; cubic=path[1]
    p1,p2,p3=cubic['control1'],cubic['control2'],cubic['point']
    points=[]
    for index in range(1001):
        t=index/1000; u=1-t
        points.append((cx+u**3*p0[0]+3*u*u*t*p1[0]+3*u*t*t*p2[0]+t**3*p3[0],
                       cy+u**3*p0[1]+3*u*u*t*p1[1]+3*u*t*t*p2[1]+t**3*p3[1]))
    mask=Image.new('1',(W,H))
    ImageDraw.Draw(mask).line(points,fill=1,width=max(1,round(case['width']*2)))
    distance=distance_transform_edt(np.asarray(mask)==0)
    return (distance>=5.0)&(distance<=17.0)


def halo_signal(image, roi):
    pixels=np.asarray(image,dtype=np.float32)
    amber=np.maximum(0.0,pixels[:,:,0]-pixels[:,:,1]*1.12)+np.maximum(0.0,pixels[:,:,1]-pixels[:,:,2]*1.08)
    return float(amber[roi].mean()) if np.any(roi) else 0.0


def core_signal(image, plan, case):
    route=next(layer for layer in plan['layers'] if layer.get('id')=='route_0')
    cx,cy=route['position']; path=route['shape']['path']; p0=path[0]['point']; cubic=path[1]
    p1,p2,p3=cubic['control1'],cubic['control2'],cubic['point']
    points=[]
    for index in range(1001):
        t=index/1000; u=1-t
        points.append((cx+u**3*p0[0]+3*u*u*t*p1[0]+3*u*t*t*p2[0]+t**3*p3[0],
                       cy+u**3*p0[1]+3*u*u*t*p1[1]+3*u*t*t*p2[1]+t**3*p3[1]))
    mask=Image.new('1',(W,H))
    ImageDraw.Draw(mask).line(points,fill=1,width=max(1,round(case['width']*2)))
    core=np.asarray(mask,dtype=bool)
    pixels=np.asarray(image,dtype=np.float32)
    luminance=pixels[:,:,0]*0.2126+pixels[:,:,1]*0.7152+pixels[:,:,2]*0.0722
    return float(luminance[core].mean()) if np.any(core) else 0.0


def decay_sample_frames(plan, frames):
    effect_layer=next(layer for layer in plan['layers'] if layer.get('effects'))
    intensity=next(track for track in effect_layer['effect_param_tracks']
                   if track['effect_id']=='light.glow' and track['param']=='intensity')
    keys=intensity['keyframes']
    return int(keys[2]['frame']), min(frames-1,int(keys[3]['frame'])+8)


def decay_intensity_reduction(signals, minimum_reduction=0.12):
    return decay_halo_reduction(signals,minimum_reduction)


def rgba(hex_color: str, alpha: float=1.0) -> list[float]:
    return [int(hex_color[i:i+2],16)/255 for i in (1,3,5)]+[alpha]


def bezier_length(p0,p1,p2,p3,steps=512):
    prev=p0; length=0.0
    for i in range(1,steps+1):
        t=i/steps; u=1-t
        point=tuple(u**3*p0[j]+3*u*u*t*p1[j]+3*u*t*t*p2[j]+t**3*p3[j] for j in range(2))
        length+=math.hypot(point[0]-prev[0],point[1]-prev[1]); prev=point
    return length


def path_commands(points):
    p0,p1,p2,p3=points
    return [{"type":"move_to","point":list(p0)}, {"type":"cubic_to","control1":list(p1),"control2":list(p2),"point":list(p3)}]


def arc_clock_keys(length, start_frame, end_frame, samples=24):
    """Linear frame clock: equal arc-length reveal increments over one shared interval."""
    count=min(samples,max(2,end_frame-start_frame))
    return [(start_frame+(end_frame-start_frame)*i/count, length*(1-i/count)) for i in range(count+1)]


def keyframe(frame,value): return {"frame":int(round(frame)),"value":value}

def track(prop, keys, easing='linear'): return {"property":prop,"keyframes":[keyframe(f,v) for f,v in keys],"easing":easing}


def make_scene(case, video):
    frames=case['duration']*FPS
    layers=[]
    background_asset=case.get('background_asset') or case.get('asset')
    if background_asset:
        layers.append({"id":"archive_plate","type":"image","asset":background_asset,"fit":"cover","size":[W-520,H],"position":[0,0],"start_frame":0,"duration_frames":frames})
        layers.append({"id":"title_rail","type":"shape","size":[520,H],"position":[W-260,H/2],"start_frame":0,"duration_frames":frames,"shape":{"type":"rect","fill":rgba("#151719",0.96)}})
    else:
        layers.append({"id":"archive_plate","type":"shape","size":[W,H],"position":[W/2,H/2],"start_frame":0,"duration_frames":frames,"shape":{"type":"rect","fill":rgba(BG)}})
        layers.append({"id":"plate_rule","type":"shape","size":[W-240,1],"position":[W/2,190],"start_frame":0,"duration_frames":frames,"shape":{"type":"rect","fill":rgba(WARM,0.24)}})
    if case.get('asset') or case.get('background_asset'):
        rail_x=1390 if case.get('asset') or case.get('evidence') else 1660
        text_width=780 if case.get('asset') or case.get('evidence') else 250
        layers.append({"id":"heading","type":"text","text":case['name'].upper(),"size":[text_width,64],"position":[rail_x,104],"style":{"font":"assets/fonts/Inter-Medium.ttf","font_size":28,"fill":IVORY},"start_frame":0,"duration_frames":frames})
        layers.append({"id":"caption","type":"text","text":case['purpose'].upper(),"size":[text_width,60],"position":[rail_x,162],"style":{"font":"assets/fonts/Inter-Regular.ttf","font_size":15,"fill":IVORY},"start_frame":0,"duration_frames":frames})
    else:
        layers.append({"id":"heading","type":"text","text":case['name'].upper(),"size":[1500,64],"position":[850,104],"style":{"font":"assets/fonts/Inter-Medium.ttf","font_size":28,"fill":IVORY},"start_frame":0,"duration_frames":frames})
        layers.append({"id":"caption","type":"text","text":case['purpose'].upper(),"size":[1500,38],"position":[850,151],"style":{"font":"assets/fonts/Inter-Regular.ttf","font_size":15,"fill":WARM},"start_frame":0,"duration_frames":frames})
    if case.get('evidence'):
        panel_center=[1650,610]; panel_size=[420,370]
        layers.append({"id":"field_note_panel","type":"shape","size":panel_size,"position":panel_center,"start_frame":0,"duration_frames":frames,
            "shape":{"type":"rect","fill":rgba("#202326",0.96),"stroke":{"color":"#77736A","width":1}}})
        layers.append({"id":"evidence_label","type":"text","text":"FIELD NOTE  /  06:42","size":[350,36],"position":[1650,485],"style":{"font":"assets/fonts/Inter-Medium.ttf","font_size":19,"fill":IVORY},"start_frame":0,"duration_frames":frames})
        layers.append({"id":"field_body","type":"text","text":"ROUTE TRACE  —  VERIFIED\nPOSITION  46° 49′ N  /  09° 31′ E\nOBSERVATION  02:17 UTC","size":[350,132],"position":[1650,600],"style":{"font":"assets/fonts/UbuntuMono-R.ttf","font_size":16,"fill":IVORY},"start_frame":0,"duration_frames":frames})
        layers.append({"id":"connection_note","type":"text","text":"ARCHIVE CONNECTION  /  ROUTE VERIFIED","size":[400,36],"position":[1650,865],"style":{"font":"assets/fonts/Inter-Regular.ttf","font_size":13,"fill":WARM},"start_frame":0,"duration_frames":frames})
        layers.append({"id":"photo_caption","type":"text","text":"ARCHIVAL PHOTO  /  SOURCE PLATE","size":[740,36],"position":[790,990],"style":{"font":"assets/fonts/Inter-Regular.ttf","font_size":13,"fill":IVORY},"start_frame":0,"duration_frames":frames})
    if case.get('evidence'):
        routes=[((-640,-120),(-420,-80),(420,-55),(640,-45))]
    elif case.get('asset'):
        routes=[((-390,170),(-230,95),(230,70),(390,-105))]
    elif case['slug']=='01_fine_stroke':
        routes=[((-145,0),(-70,-42),(78,-35),(145,0))]
    elif case['slug']=='02_soft_glow':
        routes=[((-150,15),(-80,-70),(76,65),(150,10))]
    else:
        routes=[((-150,0),(-78,-48),(85,50),(150,-5))]
    for i,points in enumerate(routes):
        length=bezier_length(*points)
        min_x=min(point[0] for point in points); max_x=max(point[0] for point in points)
        min_y=min(point[1] for point in points); max_y=max(point[1] for point in points)
        center_x=(min_x+max_x)/2; center_y=(min_y+max_y)/2
        local_points=tuple((point[0]-center_x,point[1]-center_y) for point in points)
        commands=path_commands(local_points)
        effect_pad=(case['glow']['radius']*3 if case.get('glow') else 0)+case['width']+4
        layer_size=[math.ceil(max_x-min_x+2*effect_pad),math.ceil(max_y-min_y+2*effect_pad)]
        layer_position=[W/2+center_x,H/2+center_y]
        reveal_start=24+i*10; reveal_end=min(frames-1,reveal_start+int(1.55*FPS))
        dash=[length,length] if case.get('draw') else None
        reveal_keys=arc_clock_keys(length,reveal_start,reveal_end) if dash else []
        if case.get('glow'):
            glow_stroke={"color":case['glow']['hex_color'],"width":case['width'],"cap":"round","join":"round"}
            if dash: glow_stroke.update({"dash_array":dash,"dash_offset":length})
            glow_layer={"id":f"route_glow_{i}","type":"shape","size":layer_size,"position":layer_position,"start_frame":0,"duration_frames":frames,
                "shape":{"type":"path","path":commands,"stroke":glow_stroke},
                "effects":[{"type":"glow","radius":case['glow']['radius'],"intensity":case['glow']['intensity'],"color":rgba(case['glow']['hex_color'])}]}
            glow_tracks=[]
            if case.get('draw'):
                glow_tracks.append(track('stroke_dash_offset',reveal_keys,'linear'))
            if case.get('decay'):
                glow_layer['effect_param_tracks']=[{
                    'effect_id':'light.glow','param':'intensity',
                    'keyframes':[keyframe(frame,value) for frame,value in
                        [(0,0.0),(reveal_start,case['glow']['intensity']),
                         (reveal_end,case['glow']['intensity']),(min(frames-1,reveal_end+17),0.0),
                         (frames-1,0.0)]],
                    'easing':'linear'}]
                glow_tracks.append(track('opacity',[(0,0.0),(reveal_start,0.35),(reveal_end,0.35),(frames-1,0.35)],'linear'))
            elif case.get('draw'):
                glow_tracks.append(track('opacity',[(0,0.0),(reveal_start,0.35),(reveal_end,0.35),(frames-1,0.35)],'linear'))
            else:
                glow_tracks.append(track('opacity',[(0,0.35),(frames-1,0.35)],'linear'))
            if glow_tracks: glow_layer['animation']={"tracks":glow_tracks}
            layers.append(glow_layer)
        stroke={"color":case['color'],"width":case['width'],"cap":"round","join":"round"}
        if dash: stroke.update({"dash_array":dash,"dash_offset":length})
        layer={"id":f"route_{i}","type":"shape","size":layer_size,"position":layer_position,"start_frame":0,"duration_frames":frames,
            "shape":{"type":"path","path":commands,"stroke":stroke}}
        if case.get('draw'):
            core_tracks=[track('stroke_dash_offset',reveal_keys,'linear')]
            if case.get('decay'):
                core_tracks.append(track('opacity',[(0,0.0),(reveal_start,1.0),(reveal_end,1.0),(reveal_end+17,1.0),(frames-1,1.0)],'linear'))
            layer['animation']={"tracks":core_tracks}
        layers.append(layer)
    plan={"schema":"chronon.render-plan.v3","version":3,"job_id":"glow_lines_v2_"+case['slug'],
        "canvas":{"width":W,"height":H,"fps_num":FPS,"fps_den":1,"duration_frames":frames},
        "layers":layers,
        "output":{"path":str(video),"format":"mp4","codec":"h264","color_space":"rec709"}}
    return plan


def verify_contract(plan, case):
    assert plan['schema']=='chronon.render-plan.v3'
    assert plan['canvas']=={"width":W,"height":H,"fps_num":FPS,"fps_den":1,"duration_frames":case['duration']*FPS}
    for layer in plan['layers']:
        shape=layer.get('shape',{})
        if shape.get('type')=='path' and 'stroke' in shape:
            stroke=shape['stroke']; assert 0.75<=stroke['width']<=2.0
            assert stroke['cap']=='round' and stroke['join']=='round'
            assert stroke['color'] in (IVORY,AMBER,RED,STEEL,WARM) or stroke['color']=='#C59A63'
            assert 'dash_array' not in stroke or len(stroke['dash_array'])==2
        if 'effects' in layer:
            assert len(layer['effects'])==1 and layer['effects'][0]['type']=='glow'
            assert layer['effects'][0]['radius']<=6 and layer['effects'][0]['intensity']<=0.30
        assert layer.get('type')!='particles'
    if case.get('asset') or case.get('background_asset'):
        assert any(layer.get('id')=='title_rail' for layer in plan['layers'])
    if case.get('evidence'):
        assert sum(layer.get('type')=='image' for layer in plan['layers'])==1
        assert {'archive_plate','field_note_panel','evidence_label','field_body','connection_note','photo_caption'}.issubset(
            {layer['id'] for layer in plan['layers']})
        assert not any(layer.get('id')=='duplicate_evidence_photo' for layer in plan['layers'])
        image=next(layer for layer in plan['layers'] if layer.get('type')=='image')
        assert image['size']==[W-520,H] and image['position']==[0,0]
        panel=next(layer for layer in plan['layers'] if layer.get('id')=='field_note_panel')
        assert panel['position'][0] >= W-520 and panel['position'][0]+panel['size'][0]/2 <= W
        route=next(layer for layer in plan['layers'] if layer.get('id')=='route_0')
        assert route['position'][0] < W-520 and route['position'][0]+route['size'][0]/2 > W-520
        assert not any(layer.get('type')=='image' and layer['id']!='archive_plate' for layer in plan['layers'])
    path_layers=[layer for layer in plan['layers'] if layer.get('shape',{}).get('type')=='path']
    if case.get('glow'):
        assert len(path_layers)==2, 'glow underlay and crisp core must use matching path geometry'
        assert path_layers[0]['shape']['path']==path_layers[1]['shape']['path']
    if case.get('decay'):
        effect_layer=next(layer for layer in path_layers if layer.get('effects'))
        core_layer=next(layer for layer in path_layers if not layer.get('effects'))
        opacity=next(track for track in effect_layer['animation']['tracks'] if track['property']=='opacity')
        core_opacity=next(track for track in core_layer['animation']['tracks'] if track['property']=='opacity')
        intensity=next(track for track in effect_layer['effect_param_tracks']
                       if track['effect_id']=='light.glow' and track['param']=='intensity')
        values=[key['value'] for key in intensity['keyframes']]
        opacity_values=[key['value'] for key in opacity['keyframes']]
        core_values=[key['value'] for key in core_opacity['keyframes']]
        assert values[-1] < values[1], 'glow intensity must decay while the crisp core remains'
        assert values[1] == values[2] > values[3] == values[4], 'glow must hold through reveal, then fade to zero and remain quiet'
        assert opacity_values[-1]==0.35, 'emission source opacity remains authored and consistent'
        assert core_values[-1]==1.0, 'base stroke must remain fully visible after emission decays'
    for layer in plan['layers']:
        if layer.get('id')=='photo_shade':
            raise AssertionError('large full-canvas photo_shade treatment is not an editorial plate')


def run(cmd, log):
    proc=subprocess.run(list(map(str,cmd)),cwd=REPO,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    log.parent.mkdir(parents=True,exist_ok=True); log.write_text(proc.stdout,encoding='utf-8')
    if proc.returncode: raise RuntimeError(f"exit {proc.returncode}: {' '.join(map(str,cmd))}\n{proc.stdout[-4000:]}")
    return proc.stdout


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cli',type=pathlib.Path,default=CLI_DEFAULT)
    ap.add_argument('--output-root',type=pathlib.Path,default=OUTPUT_DEFAULT)
    ap.add_argument('--validate-only',action='store_true')
    ap.add_argument('--case',action='append',choices=[c['id'] for c in CASES])
    ap.add_argument('--generate-only',action='store_true')
    args=ap.parse_args()
    cli=args.cli
    cli=(REPO/cli).resolve() if not cli.is_absolute() else cli.resolve()
    if not cli.is_file(): raise RuntimeError(f'Chronon3D CLI missing: {cli}')
    if not args.validate_only and not args.generate_only and (not shutil.which('ffmpeg') or not shutil.which('ffprobe')): ap.error('ffmpeg/ffprobe required')
    root=args.output_root.resolve(); root.mkdir(parents=True,exist_ok=True)
    selected=[c for c in CASES if not args.case or c['id'] in args.case]
    report={"suite":"Chronon3D Glow Lines 2.0","status":"NOT RUN","cases":[],"render_order":"sequential",
            "quality":{"resolution":[W,H],"fps":FPS,"output_color_space":"rec709","temporal_motion_blur":{"status":"NOT_ENABLED","reason":"native backend probe crashed before temporal-blur comparison could be established"},"encoder":"H.264 CRF 14 preset slow"},
            "environment":{"render_backend":"software","render_hardware":"none","cpu_gpu_parity":"NOT_TESTED","gpu_probe":"Vulkan native backend exited 139 during a single-frame probe"},
            "acceptance":{"state":"PARTIAL","blocking_gates":["rendered decay halo reduction >=12%","CPU/GPU parity not tested","Drive upload blocked pending approved credentials and new-folder consent"]}}
    for case in selected:
        folder=root/case['slug']; folder.mkdir(parents=True,exist_ok=True)
        video=root/(case['slug']+'.mp4'); plan_path=folder/(case['slug']+'.plan.json')
        plan=make_scene(case,video); verify_contract(plan,case)
        plan_path.write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
        item={"id":case['id'],"slug":case['slug'],"duration_seconds":case['duration'],"frames":case['duration']*FPS,
              "plan":str(plan_path),"plan_sha256":digest(plan_path),"status":"GENERATED"}
        if not args.generate_only:
            validation=run([cli,'validate','--plan',plan_path,'--assets-root',REPO/'Chronon3d'],folder/'validate.log')
            try:
                start=validation.rfind('\n{')
                decoded=json.loads(validation[start+1:] if start>=0 else validation[validation.find('{'):])
                if decoded.get('status')!='PASS' or decoded.get('valid') is not True: raise RuntimeError(str(decoded))
            except (ValueError,KeyError) as exc:
                raise RuntimeError(f'could not confirm canonical plan validation for {case["slug"]}: {validation[-1500:]}') from exc
            item['validation']='PASS'
            if not args.validate_only:
                started=time.monotonic()
                render_cmd=[cli,'render','--plan',plan_path,'--backend','software','--hardware','none','--fps','30','--crf','14','--encode-preset','slow','--encoder-backend','pipe','--color-output','rec709','--assets-root',REPO/'Chronon3d','-o',video,'--report']
                run(render_cmd,folder/'render.log')
                probe=json.loads(run(['ffprobe','-v','error','-select_streams','v:0','-show_entries','stream=codec_name,width,height,r_frame_rate,nb_frames,duration,pix_fmt,bits_per_raw_sample,profile','-show_entries','format=duration','-of','json',video],folder/'ffprobe.log'))
                timing_path=pathlib.Path(str(video)+'.timing.json')
                if not timing_path.is_file(): raise RuntimeError('renderer produced no timing sidecar')
                timing=json.loads(timing_path.read_text())
                timing_blob=json.dumps(timing,allow_nan=False)
                if not timing_blob or not timing.get('job'):
                    raise RuntimeError('timing sidecar missing job evidence or contains invalid numbers')
                s=probe['streams'][0]; expected=case['duration']*FPS
                if s.get('codec_name')!='h264' or s.get('width')!=W or s.get('height')!=H or s.get('r_frame_rate')!='30/1' or int(s.get('nb_frames',-1))!=expected:
                    raise RuntimeError(f'video stream mismatch: {s}')
                if s.get('pix_fmt')!='yuv420p' or s.get('duration')!=f"{case['duration']:.6f}":
                    raise RuntimeError(f'pixel format or exact duration mismatch: {s}')
                proof_frames=[('start',0),('mid',expected//2),('end',expected-1)]
                decay_case=bool(case.get('decay'))
                if decay_case:
                    decay_pre_frame,decay_post_frame=decay_sample_frames(plan,expected)
                    proof_frames += [('decay_baseline',0),('decay_pre',decay_pre_frame),('decay_post',decay_post_frame)]
                sample_frames=[]
                for label,frame in proof_frames:
                    png=folder/f'{label}.png'
                    run(['ffmpeg','-v','error','-y','-i',video,'-vf',f'select=eq(n\\,{frame})','-frames:v','1','-vsync','0',png],folder/f'extract_{label}.log')
                    if not png.is_file() or png.stat().st_size<1000: raise RuntimeError(f'empty representative frame {label}')
                    image=Image.open(png).convert('RGB'); from PIL import ImageStat; stats=ImageStat.Stat(image)
                    if max(stats.stddev)<1.0: raise RuntimeError(f'visually near-blank representative frame {label}')
                    sample_frames.append({"label":label,"frame":frame,"sha256":digest(png),"bytes":png.stat().st_size,"mean_rgb":[round(v,3) for v in stats.mean],"stddev_rgb":[round(v,3) for v in stats.stddev]})
                decay_measurement=None
                if decay_case:
                    baseline=Image.open(folder/'decay_baseline.png').convert('RGB')
                    pre=Image.open(folder/'decay_pre.png').convert('RGB')
                    post=Image.open(folder/'decay_post.png').convert('RGB')
                    halo_roi=route_halo_roi(plan,case)
                    baseline_signal=halo_signal(baseline,halo_roi)
                    pre_signal=halo_signal(pre,halo_roi); post_signal=halo_signal(post,halo_roi)
                    try:
                        reduction=decay_halo_reduction([baseline_signal,pre_signal,post_signal],minimum_reduction=0.12)
                    except ValueError as exc:
                        raise RuntimeError(f'quantitative glow-decay gate failed: {exc}') from exc
                    baseline_core=core_signal(baseline,plan,case)
                    post_core=core_signal(post,plan,case)
                    if post_core <= baseline_core+1.0:
                        raise RuntimeError(f'crisp route core is not measurably visible after glow decay: baseline={baseline_core:.3f}, post={post_core:.3f}')
                    decay_measurement={"baseline_frame":0,"pre_frame":decay_pre_frame,"post_frame":decay_post_frame,"baseline_halo_signal":round(reduction['baseline_halo_signal'],5),"pre_halo_signal":round(reduction['pre_halo_signal'],5),"post_halo_signal":round(reduction['post_halo_signal'],5),"pre_halo_excess":round(reduction['pre_halo_excess'],5),"post_halo_excess":round(reduction['post_halo_excess'],5),"relative_reduction":round(reduction['relative_reduction'],5),"baseline_core_luma":round(baseline_core,5),"post_core_luma":round(post_core,5),"core_opacity_after_decay":1.0}
                job=timing.get('job',{})
                if not timing_blob or not job:
                    raise RuntimeError('timing sidecar missing job evidence or contains invalid numbers')
                for key in ('prepare_ms','render_loop_wall_ms'):
                    value=job.get(key)
                    if not isinstance(value,(int,float)) or not math.isfinite(value) or value<0:
                        raise RuntimeError(f'timing sidecar job.{key} must be finite and non-negative')
                timeline=job.get('cpu_breakdown',{}).get('timeline_eval_ms')
                if not isinstance(timeline,(int,float)) or not math.isfinite(timeline) or timeline<0:
                    raise RuntimeError('timing sidecar job.cpu_breakdown.timeline_eval_ms must be finite and non-negative')
                if not isinstance(job.get('job_wall_ms'),(int,float)) or not math.isfinite(job['job_wall_ms']) or job['job_wall_ms']<=0:
                    raise RuntimeError('timing sidecar job_wall_ms must be positive and finite')
                item['timing_finite_json_values']=True
                job_summary={key:job.get(key) for key in ('job_wall_ms','prepare_ms','render_loop_wall_ms','encoder_finalize_ms','execution_path')}
                job_summary['cpu_breakdown']={"timeline_eval_ms":timeline}
                job_summary['effect_usage']=job.get('effect_usage',{})
                item['motion_blur']='NOT_ENABLED'
                if case.get('glow'):
                    glow_receipt=job.get('effect_usage',{}).get('light.glow',{})
                    if glow_receipt.get('software_dispatches',0)<=0 or glow_receipt.get('admission_rejected_frames',0)>0:
                        raise RuntimeError(f'glow effect receipt missing successful dispatches: {glow_receipt}')
                    if glow_receipt.get('fallback_count',0)>0 or glow_receipt.get('fallback_effect_time_ns',0)>0:
                        raise RuntimeError(f'glow effect receipt reports fallback work: {glow_receipt}')
                item.update({"status":"PASS","video":str(video),"video_sha256":digest(video),"video_bytes":video.stat().st_size,
                    "ffprobe":s,"timing_sidecar":str(timing_path),"timing_job":job_summary,"representative_frames":sample_frames,"render_wall_seconds":round(time.monotonic()-started,2)})
                if decay_measurement:
                    item['glow_decay_measurement']=decay_measurement
        report['cases'].append(item)
    statuses=[item.get('status') for item in report['cases']]
    all_six_selected=(len(selected)==len(CASES) and {c['id'] for c in selected}=={c['id'] for c in CASES})
    render_complete=bool(statuses) and all(status=='PASS' for status in statuses)
    report['status']='PASS' if render_complete and all_six_selected else ('VALIDATED' if statuses and all(item.get('validation')=='PASS' for item in report['cases']) else 'GENERATED')
    report['acceptance']['state']='PASS' if render_complete and all_six_selected else 'PARTIAL'
    if render_complete and all_six_selected:
        report['acceptance']['blocking_gates']=[gate for gate in report['acceptance']['blocking_gates'] if gate != 'rendered decay halo reduction >=12%']
    report_path=root/'report.json'; report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))
    return 0


if __name__=='__main__':
    try: raise SystemExit(main())
    except Exception as exc:
        error_report={'suite':'Chronon3D Glow Lines 2.0','status':'FAIL','acceptance':{'state':'PARTIAL','blocking_gates':[str(exc)]},'cases':[]}
        output_arg=None
        try:
            args=sys.argv[1:]
            if '--output-root' in args:
                output_arg=pathlib.Path(args[args.index('--output-root')+1]).resolve()
            if output_arg:
                output_arg.mkdir(parents=True,exist_ok=True)
                (output_arg/'report.json').write_text(json.dumps(error_report,indent=2)+'\n',encoding='utf-8')
        except Exception as report_error:
            print(f'FAIL: {exc}; also could not write failure report: {report_error}',file=sys.stderr)
            raise SystemExit(1)
        print(f'FAIL: {exc}',file=sys.stderr); raise SystemExit(1)
