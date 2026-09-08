import serial, time, subprocess, threading, os, sys
sys.path.insert(0, os.path.dirname(__file__))
import run_test as rt

TESTS_DIR = os.path.dirname(__file__)

# Method 1: Manual serial.Serial + raw read
ser = serial.Serial('COM18', 115200, timeout=0.1)
data = bytearray()
stop = threading.Event()
def r():
    while not stop.is_set():
        try:
            d = ser.read(4096)
            if d:
                data.extend(d)
        except Exception:
            break
t = threading.Thread(target=r, daemon=True)
t.start()

# Run test.sh
env = dict(os.environ)
env['SKIP_LOAD'] = 'y'
env['AGENT_RUN'] = 'y'
env['I3C_TEST_ROOT'] = os.path.abspath(os.path.join(TESTS_DIR, '..'))
bash = r'C:\Program Files\Git\bin\bash.exe'
test_sh = os.path.join(TESTS_DIR, 'case_mbox_basic', 'test.sh')
proc = subprocess.Popen(
    [bash, test_sh, '-r'],
    cwd=env['I3C_TEST_ROOT'], env=env,
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

time.sleep(12)
proc.kill()
stop.set()
ser.close()

print(f'Method 1 (manual serial): {len(data)} bytes')
has1 = b'PROJECT EXECUTION: SUCCESSFUL' in data
print(f'  has SUCCESS: {has1}')

# Method 2: Using SerialCapture
time.sleep(2)
subprocess.run(['taskkill', '/F', '/IM', 'riscv64-unknown-elf-gdb.exe'],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
time.sleep(2)

ctrl = rt.SerialCapture('COM18')
ctrl.start()
print(f'SerialCapture thread alive: {ctrl._thread.is_alive()}')

proc2 = subprocess.Popen(
    [bash, test_sh, '-r'],
    cwd=env['I3C_TEST_ROOT'], env=env,
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

time.sleep(12)
proc2.kill()
ctrl.stop()
print(f'Method 2 (SerialCapture): {len(ctrl.lines)} lines, {len(ctrl.text)} chars')
has2 = 'PROJECT EXECUTION: SUCCESSFUL' in ctrl.text
print(f'  has SUCCESS: {has2}')
