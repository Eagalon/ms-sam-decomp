"""tests/lib: the sam_tts library (sam.exe) must produce byte-identical WAVs to sam_say.exe.

Runs a corpus of varied lines through both CLIs with Sam, Mike and Mary and a few SAPI 4 effects and
compares the files byte for byte.

    python tests/lib/compare_lib.py [--arch x64]
"""
import hashlib
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ARCH = "x64"
if "--arch" in sys.argv:
    ARCH = sys.argv[sys.argv.index("--arch") + 1]
EXT = ".exe" if os.name == "nt" else ""
BUILD = os.path.join(ROOT, "build", ARCH) if os.path.isdir(os.path.join(ROOT, "build", ARCH)) else os.path.join(ROOT, "build")
SAY = os.path.join(BUILD, "sam_say" + EXT)
LIB = os.path.join(BUILD, "sam" + EXT)
DATA = os.path.join(ROOT, "data", "voices")
TMP = os.path.join(ROOT, "build", "libtest")

CORPUS = os.path.join(ROOT, "ref", "corpus", "sentences.txt")  # only in the working tree, not in the repo
LINES = [l.strip() for l in open(CORPUS, encoding="utf-8") if l.strip()] if os.path.exists(CORPUS) else []
EXTRA = [
    "I read the book yesterday. I will read it tomorrow.",
    "They live near the live wire.",
    "Please record the record.",
    "The wind will wind the clock.",
    "Hello there, how are you doing today?",
    "I cannot believe it; she said no: never again!",
    "Blorfing zorblats don't care.",
    "I'm sure we'll see it's fine.",
    "The NASA and FBI agents met Dr. Smith on Main St.",
    "Well-known e-mail addresses.",
    'Some "quoted" words (in parentheses) here.',
    "It costs $1,234.56 on 3/4/2021 at 10:30 PM.",
    "Call 1-800-555-1234 now, or 555-0199 later.",
    "The 3rd of May, 1996, was the 42nd day.",
    "One half plus 3/4 equals 1.25.",
    "Dr. Smith vs. Mr. Jones at 5 p.m.",
    "A B C D E F G.",
    "What?! Really... no way.",
    "The temperature is -5 degrees.",
    "Version 2.0.1 of the software.",
]

CASES = []
for i, line in enumerate(LINES + EXTRA):
    CASES.append(("c%02d" % i, ["--voice", "Sam"], line))
for i, line in enumerate(EXTRA[:12]):
    CASES.append(("m%02d" % i, ["--voice", "Mike"], line))
for i, line in enumerate(EXTRA[:12]):
    CASES.append(("y%02d" % i, ["--voice", "Mary"], line))
for i, (fx, line) in enumerate([("hall", EXTRA[0]), ("stadium", EXTRA[1]), ("space", EXTRA[2]),
                                ("whisper", EXTRA[3]), ("robosoft1", EXTRA[4]), ("robosoft3", EXTRA[5]),
                                ("monotone", EXTRA[6]), ("room", EXTRA[7]), ("robosoft2", EXTRA[8]),
                                ("robosoft6", EXTRA[9])]):
    CASES.append(("f%02d" % i, ["--voice", "Sam", "--effect", fx], line))
for i, (v, fx, line) in enumerate([("Mike", "hall", EXTRA[10]), ("Mary", "whisper", EXTRA[11]),
                                   ("Mary", "robosoft1", EXTRA[0]), ("Mike", "space", EXTRA[1])]):
    CASES.append(("g%02d" % i, ["--voice", v, "--effect", fx], line))


def md5(p):
    return hashlib.md5(open(p, "rb").read()).hexdigest()


def main():
    os.makedirs(TMP, exist_ok=True)
    bad = []
    for name, args, line in CASES:
        a = os.path.join(TMP, name + "_say.wav")
        b = os.path.join(TMP, name + "_lib.wav")
        for exe, out in ((SAY, a), (LIB, b)):
            r = subprocess.run([exe, "--data", DATA] + args + [line, out], capture_output=True)
            if r.returncode != 0:
                print("FAIL running", os.path.basename(exe), name, r.stderr.decode(errors="replace"))
                bad.append(name)
        if name not in bad and md5(a) != md5(b):
            bad.append(name)
            print("DIFF", name, args, repr(line))
    print("%d/%d cases byte-identical" % (len(CASES) - len(bad), len(CASES)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
