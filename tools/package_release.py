"""Package a Release DLL with its documentation and applicable license files."""
import argparse, pathlib, zipfile
p=argparse.ArgumentParser()
p.add_argument("dll",type=pathlib.Path)
p.add_argument("output",type=pathlib.Path)
a=p.parse_args()
source=pathlib.Path(__file__).resolve().parent.parent
a.output.parent.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(a.output,"w",zipfile.ZIP_DEFLATED) as z:
    z.write(a.dll,a.dll.name)
    for name in ["README.md","INSTALL.md","LICENSE","HOOK_INVENTORY.md","THIRD_PARTY_NOTICES.md","third_party/nlohmann/LICENSE.MIT","third_party/PluginSDK-LICENSE"]:
        z.write(source/name,name)
