# Force the vector table to the app slot the TBS bootloader jumps to.
Import("env")

def fix(e):
    defs = [d for d in e.get("CPPDEFINES", [])
            if not (isinstance(d, (tuple, list)) and d[0] == "VECT_TAB_OFFSET") and d != "VECT_TAB_OFFSET"]
    e.Replace(CPPDEFINES=defs)
    e.Append(CPPDEFINES=[("VECT_TAB_OFFSET", "0xC000")])

fix(env)
for lb in env.GetLibBuilders():
    fix(lb.env)
