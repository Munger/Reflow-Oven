#!/usr/bin/env python3
## @file gen_api_ref.py
##
## @brief Markdown API reference generator — reads Doxygen XML, writes a
##        module-by-module Markdown reference to the output file.
##
## Only public header files are included; static functions and .c files
## are skipped. Invoked automatically by the CMake docs target after
## Doxygen runs.
##
## Usage: gen_api_ref.py \<xml_dir\> \<output_file\>
##
## @copyright Copyright (c) 2026 Tim Hosking
## @see https://github.com/munger
## @par Licence: MIT

import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def elem_text(elem):
    if elem is None:
        return ""
    return "".join(elem.itertext()).strip()


def brief(parent):
    return elem_text(parent.find("briefdescription"))


def detail_paras(parent):
    dd = parent.find("detaileddescription")
    if dd is None:
        return []
    paras = []
    for para in dd.findall("para"):
        if para.find("parameterlist") is not None:
            continue
        if para.find("simplesect") is not None:
            continue
        t = elem_text(para)
        if t:
            paras.append(t)
    return paras


def param_docs(parent):
    dd = parent.find("detaileddescription")
    if dd is None:
        return {}
    result = {}
    for pl in dd.findall(".//parameterlist[@kind='param']"):
        for item in pl.findall("parameteritem"):
            names = [elem_text(n) for n in item.findall(".//parametername")]
            desc  = elem_text(item.find("parameterdescription"))
            for name in names:
                result[name] = desc
    return result


def return_doc(parent):
    dd = parent.find("detaileddescription")
    if dd is None:
        return ""
    for ss in dd.findall(".//simplesect[@kind='return']"):
        return elem_text(ss)
    return ""


def func_signature(mdef):
    ret  = elem_text(mdef.find("type"))
    name = elem_text(mdef.find("name"))
    params = []
    for param in mdef.findall("param"):
        ptype = elem_text(param.find("type"))
        pname = elem_text(param.find("declname")) if param.find("declname") is not None else ""
        params.append(f"{ptype} {pname}".strip() if pname else ptype)
    if not params:
        params = ["void"]
    return f"{ret} {name}( {', '.join(params)} )"


def load_struct(xml_dir, refid):
    path = xml_dir / f"{refid}.xml"
    if not path.exists():
        return "", []
    root  = ET.parse(path).getroot()
    cdef  = root.find("compounddef")
    sbr   = brief(cdef) if cdef is not None else ""
    fields = []
    for mdef in root.findall(".//memberdef[@kind='variable']"):
        fields.append((
            elem_text(mdef.find("type")),
            elem_text(mdef.find("name")),
            brief(mdef),
        ))
    return sbr, fields


def process_file(xml_dir, refid, filename):
    path = xml_dir / f"{refid}.xml"
    if not path.exists():
        return None

    root = ET.parse(path).getroot()
    cdef = root.find("compounddef")
    if cdef is None:
        return None

    out = []
    module    = Path(filename).stem
    file_brief = brief(cdef)

    out.append(f"## {module}\n")
    if file_brief:
        out.append(f"*{file_brief}*\n")

    # Enums
    enums = []
    for sdef in cdef.findall("sectiondef[@kind='enum']"):
        for mdef in sdef.findall("memberdef[@kind='enum']"):
            values = [
                (elem_text(ev.find("name")), brief(ev))
                for ev in mdef.findall("enumvalue")
            ]
            enums.append((elem_text(mdef.find("name")), brief(mdef), values))

    # Structs via innerclass refs
    structs = []
    for ic in cdef.findall("innerclass"):
        refid_s = ic.get("refid", "")
        if not refid_s.startswith("struct"):
            continue
        sname       = elem_text(ic)
        sbr, fields = load_struct(xml_dir, refid_s)
        structs.append((sname, sbr, fields))

    if enums or structs:
        out.append("### Types\n")

    for ename, ebr, values in enums:
        out.append(f"#### `{ename}`\n")
        if ebr:
            out.append(f"{ebr}\n")
        if values:
            out.append("| Value | Description |")
            out.append("|-------|-------------|")
            for vname, vbr in values:
                out.append(f"| `{vname}` | {vbr} |")
            out.append("")

    for sname, sbr, fields in structs:
        out.append(f"#### `{sname}`\n")
        if sbr:
            out.append(f"{sbr}\n")
        if fields:
            out.append("| Type | Field | Description |")
            out.append("|------|-------|-------------|")
            for ftype, fname, fbr in fields:
                out.append(f"| `{ftype}` | `{fname}` | {fbr} |")
            out.append("")

    # Functions (public only)
    funcs = []
    for sdef in cdef.findall("sectiondef[@kind='func']"):
        for mdef in sdef.findall("memberdef[@kind='function']"):
            if mdef.get("static") == "yes":
                continue
            funcs.append((
                func_signature(mdef),
                brief(mdef),
                detail_paras(mdef),
                param_docs(mdef),
                return_doc(mdef),
            ))

    if funcs:
        out.append("### Functions\n")

    for sig, fbr, fdet, fparams, fret in funcs:
        fname = sig.split("(")[0].split()[-1]
        out.append(f"#### `{fname}`\n")
        out.append("```c")
        out.append(sig)
        out.append("```\n")
        if fbr:
            out.append(f"{fbr}\n")
        for para in fdet:
            out.append(f"{para}\n")
        if fparams:
            out.append("| Parameter | Description |")
            out.append("|-----------|-------------|")
            for pname, pdesc in fparams.items():
                out.append(f"| `{pname}` | {pdesc} |")
            out.append("")
        if fret:
            out.append(f"**Returns:** {fret}\n")

    out.append("---\n")
    return "\n".join(out)


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <xml_dir> <output_file>")
        sys.exit(1)

    xml_dir = Path(sys.argv[1])
    output  = Path(sys.argv[2])

    index_path = xml_dir / "index.xml"
    if not index_path.exists():
        print(f"Error: {index_path} not found — run Doxygen first.")
        sys.exit(1)

    index    = ET.parse(index_path)
    sections = []

    for compound in index.getroot().findall("compound[@kind='file']"):
        name  = elem_text(compound.find("name"))
        refid = compound.get("refid")
        if not name.endswith(".h"):
            continue
        md = process_file(xml_dir, refid, name)
        if md:
            sections.append((name, md))

    sections.sort(key=lambda x: x[0])

    with open(output, "w") as f:
        f.write("# API Reference\n\n")
        f.write("_Auto-generated from source. Run `ninja docs` to update._\n\n")
        f.write("---\n\n")
        for _, md in sections:
            f.write(md)
            f.write("\n")

    print(f"Generated {output} ({len(sections)} modules)")


if __name__ == "__main__":
    main()
