import ctypes
from ctypes import wintypes

k = ctypes.windll.kernel32
k.CreateFileW.restype = wintypes.HANDLE
k.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                          ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
FILE_SHARE_READ = 0x1
FILE_SHARE_WRITE = 0x2
INVALID = wintypes.HANDLE(-1).value

for num in (5, 16, 17):
    name = "\\\\.\\COM%d" % num
    for share, label in ((0, "excl"), (FILE_SHARE_READ | FILE_SHARE_WRITE, "share")):
        h = k.CreateFileW(name, GENERIC_READ | GENERIC_WRITE, share, None, OPEN_EXISTING, 0, None)
        if h == INVALID:
            print(name, label, "err=", k.GetLastError())
        else:
            print(name, label, "OPENED ok")
            k.CloseHandle(h)
