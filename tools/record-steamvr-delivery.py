"""Read-only, separate-process OpenVR timing capture; never submits VR frames."""
import argparse
import ctypes
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).parent / 'openvr-probe-deps'))
import openvr

parser = argparse.ArgumentParser()
parser.add_argument('--pid', type=int, required=True)
parser.add_argument('--seconds', type=float, default=120)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if not 0 < args.seconds <= 300:
    parser.error('duration must be within 0..300 seconds')
args.output.parent.mkdir(parents=True, exist_ok=True)

def scalar_fields(value):
    return {name: getattr(value, name) for name, *_ in value._fields_
            if isinstance(getattr(value, name), (int, float))}

system = openvr.init(openvr.VRApplication_Background)
try:
    compositor = openvr.VRCompositor()
    stats = compositor.getCumulativeStats(ctypes.sizeof(openvr.Compositor_CumulativeStats))
    if stats.m_nPid != args.pid:
        raise RuntimeError(f'Wrong compositor application: {stats.m_nPid}, expected {args.pid}')
    hz = system.getFloatTrackedDeviceProperty(openvr.k_unTrackedDeviceIndex_Hmd,
                                              openvr.Prop_DisplayFrequency_Float)
    start = time.monotonic()
    last_index = -1
    last_stats = -1
    frames = 0
    with args.output.open('x', encoding='utf-8') as out:
        def write(kind, **data):
            out.write(json.dumps(dict(kind=kind, elapsed=time.monotonic()-start,
                                      unix=time.time(), **data))+'\n')
        write('start', pid=args.pid, hz=hz, stats=scalar_fields(stats))
        print(json.dumps(dict(state='recording', pid=args.pid, hz=hz, output=str(args.output))), flush=True)
        while time.monotonic()-start < args.seconds:
            now = time.monotonic()-start
            if now-last_stats >= 1:
                stats = compositor.getCumulativeStats(ctypes.sizeof(openvr.Compositor_CumulativeStats))
                focus = compositor.getCurrentSceneFocusProcess()
                renderer = compositor.getLastFrameRenderer()
                write('stats', stats=scalar_fields(stats), focus=focus, renderer=renderer)
                if stats.m_nPid != args.pid:
                    raise RuntimeError('Scene application changed during capture')
                last_stats = now
                out.flush()
            timing = (openvr.Compositor_FrameTiming * 32)()
            timing[0].m_nSize = ctypes.sizeof(openvr.Compositor_FrameTiming)
            count, timing = compositor.getFrameTimings(timing)
            # Only records older than a newer *presented* frame have settled.
            # A fixed two-record delay can classify an in-flight frame as lost
            # when a driver predicts farther ahead. Keep the newest presented
            # record as well: it may still be scanned out again.
            newest_presented = next((i for i in range(count-1, -1, -1)
                                     if timing[i].m_nNumFramePresents > 0), 0)
            for frame in timing[:newest_presented]:
                if frame.m_nFrameIndex <= last_index:
                    continue
                write('frame', frame=scalar_fields(frame))
                last_index = frame.m_nFrameIndex
                frames += 1
            time.sleep(.05)
        write('end', frames=frames,
              stats=scalar_fields(compositor.getCumulativeStats(ctypes.sizeof(openvr.Compositor_CumulativeStats))))
    print(json.dumps(dict(state='complete', frames=frames, elapsed=time.monotonic()-start)), flush=True)
finally:
    openvr.shutdown()
