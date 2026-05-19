#!/usr/bin/env python3
"""
jlc.py - JLCPCB parts search tool
Uses the jlcsearch.tscircuit.com API (no auth required)

Usage examples:
  python jlc.py "1k resistor"                        # general search
  python jlc.py "ESP32" --cheapest                   # find cheapest
  python jlc.py "1k" --type resistors --basic        # basic parts only
  python jlc.py "1k" --type resistors --basic --power 0.25
  python jlc.py --type resistors --resistance 1000 --package 0402 --basic
  python jlc.py --type capacitors --capacitance 100n --package 0402
  python jlc.py --bom bom.csv                        # check a whole BOM
  python jlc.py "C8734"                              # look up by LCSC number
"""

import argparse
import json
import sys
import urllib.request
import urllib.parse
import csv

BASE_URL = "https://jlcsearch.tscircuit.com"

COMPONENT_TYPES = [
    "resistors", "capacitors", "inductors", "transistors", "mosfets",
    "diodes", "leds", "crystals", "voltage_regulators", "microcontrollers",
    "opamps", "comparators", "logic_gates", "switches", "connectors",
    "sensors", "displays", "memory", "optocouplers", "ferrite_beads",
]


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "jlc-search-tool/1.0"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read().decode())


def format_price(price):
    if price is None:
        return "N/A"
    return f"${float(price):.4f}"


def get_price(component):
    return component.get("price") or component.get("price1")


def flag_str(component):
    if component.get("is_basic"):
        return "\033[92mBASIC\033[0m"
    if component.get("is_preferred"):
        return "\033[94mPREF \033[0m"
    return "\033[90mEXT  \033[0m"


def print_components(components, limit=20):
    if not components:
        print("No components found.")
        return

    components = components[:limit]
    print()
    print(f"{'Type':<7} {'LCSC':<10} {'MFR/Part':<30} {'Package':<20} {'Stock':>8} {'Price':>8}  Description")
    print("-" * 110)
    for c in components:
        lcsc    = f"C{c.get('lcsc', '?')}"
        mfr     = str(c.get("mfr", ""))[:29]
        package = str(c.get("package", ""))[:19]
        stock   = c.get("stock", 0)
        price   = format_price(get_price(c))
        desc    = str(c.get("description", ""))[:50]
        fstr    = flag_str(c)
        stock_s = f"{stock:,}" if stock else "0"
        print(f"{fstr} {lcsc:<10} {mfr:<30} {package:<20} {stock_s:>8} {price:>8}  {desc}")
    print()
    print(f"Showing {len(components)} result(s).  BASIC = no extra setup fee  PREF = preferred extended part")


def search_general(query, basic_only=False, preferred_only=False, package=None,
                   limit=20, cheapest=False, full=True):
    params = {"q": query, "limit": str(limit * 3 if cheapest else limit), "full": "true" if full else "false"}
    if package:
        params["package"] = package
    url = f"{BASE_URL}/api/search?{urllib.parse.urlencode(params)}"
    data = fetch(url)
    components = data.get("components", [])
    return filter_and_sort(components, basic_only, preferred_only, cheapest, limit)


def search_typed(component_type, query=None, basic_only=False, preferred_only=False,
                 package=None, resistance=None, capacitance=None, power=None,
                 limit=20, cheapest=False, extra_params=None):
    params = {"full": "true", "limit": str(limit * 3 if cheapest else limit)}
    if query:
        params["search"] = query
    if package:
        params["package"] = package
    if resistance:
        params["resistance"] = str(resistance)
    if capacitance:
        params["capacitance"] = str(capacitance)
    if power:
        params["power"] = str(power)
    if extra_params:
        params.update(extra_params)

    url = f"{BASE_URL}/{component_type}/list.json?{urllib.parse.urlencode(params)}"
    data = fetch(url)
    # Typed endpoints key results by component type (e.g. "resistors"), not "components"
    components = data.get("components", data.get(component_type, []))
    return filter_and_sort(components, basic_only, preferred_only, cheapest, limit)


def filter_and_sort(components, basic_only, preferred_only, cheapest, limit):
    if basic_only:
        components = [c for c in components if c.get("is_basic")]
    elif preferred_only:
        components = [c for c in components if c.get("is_preferred")]

    if cheapest:
        components = [c for c in components if get_price(c) is not None and c.get("stock", 0) > 0]
        components.sort(key=lambda c: float(get_price(c) or 9999))

    return components[:limit]


def parse_resistance(s):
    """Parse resistance string: '1k', '10k', '4.7k', '1M', '100', '1000' -> ohms as string"""
    s = s.strip().upper().replace("OHM", "").replace("Ω", "")
    # Handle "4R7" notation (R as decimal point, e.g. 4R7 = 4.7Ω)
    if "R" in s and not s.endswith("R"):
        s = s.replace("R", ".")
        return str(float(s))
    s = s.replace("R", "")
    if s.endswith("M"):
        return str(int(float(s[:-1]) * 1_000_000))
    if s.endswith("K"):
        return str(int(float(s[:-1]) * 1_000))
    return s


def parse_capacitance(s):
    """Parse capacitance: '100n', '10u', '100p', '4.7u' -> farads as string for API"""
    s = s.strip().lower().replace("f", "").replace("farad", "")
    if s.endswith("u") or s.endswith("µ"):
        return f"{float(s[:-1]):.10f}".rstrip("0").rstrip(".") + "e-6"
    if s.endswith("n"):
        return f"{float(s[:-1]):.10f}".rstrip("0").rstrip(".") + "e-9"
    if s.endswith("p"):
        return f"{float(s[:-1]):.10f}".rstrip("0").rstrip(".") + "e-12"
    return s


def check_bom(bom_file, basic_only=False):
    """Read a CSV BOM and look up each part by LCSC number or description."""
    results = []
    with open(bom_file, newline="", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    print(f"\nChecking BOM: {bom_file} ({len(rows)} lines)\n")

    for row in rows:
        # Try to find LCSC column
        lcsc_num = None
        for key in row:
            if "lcsc" in key.lower() or key.lower() in ("c", "part", "component"):
                val = row[key].strip().lstrip("Cc")
                if val.isdigit():
                    lcsc_num = val
                    break

        if lcsc_num:
            query = f"C{lcsc_num}"
        else:
            # Fall back to description or value
            query = row.get("Value", row.get("Description", row.get("Comment", ""))).strip()

        if not query:
            continue

        try:
            data = fetch(f"{BASE_URL}/api/search?q={urllib.parse.quote(query)}&limit=1&full=true")
            comps = data.get("components", [])
            if comps:
                c = comps[0]
                status = "BASIC" if c.get("is_basic") else ("PREF" if c.get("is_preferred") else "EXT")
                results.append({
                    "query": query,
                    "lcsc": f"C{c.get('lcsc','')}",
                    "mfr": c.get("mfr", ""),
                    "package": c.get("package", ""),
                    "stock": c.get("stock", 0),
                    "price": get_price(c),
                    "status": status,
                })
            else:
                results.append({"query": query, "lcsc": "NOT FOUND", "mfr": "", "package": "",
                                 "stock": 0, "price": None, "status": "???"})
        except Exception as e:
            results.append({"query": query, "lcsc": f"ERROR: {e}", "mfr": "", "package": "",
                             "stock": 0, "price": None, "status": "ERR"})

    print(f"{'Query':<20} {'LCSC':<10} {'MFR':<28} {'Package':<18} {'Stock':>8} {'Price':>8}  {'Type'}")
    print("-" * 105)
    for r in results:
        stock_s = f"{r['stock']:,}" if r['stock'] else "0"
        price_s = format_price(r['price'])
        print(f"{r['query']:<20} {r['lcsc']:<10} {r['mfr']:<28} {r['package']:<18} {stock_s:>8} {price_s:>8}  {r['status']}")
    print()


def list_categories():
    data = fetch(f"{BASE_URL}/categories/list.json")
    cats = data.get("categories", data.get("subcategories", []))
    print("\nAvailable categories:")
    for c in cats:
        print(f"  {c}")
    print("\nComponent type endpoints (use with --type):")
    for t in COMPONENT_TYPES:
        print(f"  {t}")


def main():
    parser = argparse.ArgumentParser(
        description="Search JLCPCB parts via jlcsearch.tscircuit.com",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument("query", nargs="?", help="Search query (part number, description)")
    parser.add_argument("--type", "-t", metavar="TYPE",
                        help=f"Component type endpoint, e.g. resistors, capacitors, mosfets")
    parser.add_argument("--basic", "-b", action="store_true", help="Basic parts only (no setup fee)")
    parser.add_argument("--preferred", action="store_true", help="Preferred extended parts only")
    parser.add_argument("--cheapest", "-c", action="store_true", help="Sort by price, cheapest first")
    parser.add_argument("--package", "-p", metavar="PKG", help="Filter by package (e.g. 0402, SOT-23, SOIC-8)")
    parser.add_argument("--resistance", "-r", metavar="VAL",
                        help="Resistance filter (e.g. 1k, 10k, 4.7k, 1M)")
    parser.add_argument("--capacitance", metavar="VAL",
                        help="Capacitance filter (e.g. 100n, 10u, 100p)")
    parser.add_argument("--power", metavar="WATTS", type=float,
                        help="Power rating in watts (e.g. 0.25 for 1/4W)")
    parser.add_argument("--limit", "-n", type=int, default=20, help="Max results (default 20)")
    parser.add_argument("--bom", metavar="FILE", help="Check a BOM CSV file")
    parser.add_argument("--categories", action="store_true", help="List available categories")
    parser.add_argument("--json", action="store_true", dest="output_json", help="Output raw JSON")

    args = parser.parse_args()

    if args.categories:
        list_categories()
        return

    if args.bom:
        check_bom(args.bom, basic_only=args.basic)
        return

    try:
        if args.type:
            resistance = parse_resistance(args.resistance) if args.resistance else None
            capacitance = parse_capacitance(args.capacitance) if args.capacitance else None
            components = search_typed(
                component_type=args.type,
                query=args.query,
                basic_only=args.basic,
                preferred_only=args.preferred,
                package=args.package,
                resistance=resistance,
                capacitance=capacitance,
                power=args.power,
                limit=args.limit,
                cheapest=args.cheapest,
            )
        elif args.query:
            components = search_general(
                query=args.query,
                basic_only=args.basic,
                preferred_only=args.preferred,
                package=args.package,
                limit=args.limit,
                cheapest=args.cheapest,
            )
        else:
            parser.print_help()
            return

        if args.output_json:
            print(json.dumps(components, indent=2))
        else:
            print_components(components, limit=args.limit)

    except urllib.error.URLError as e:
        print(f"Network error: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()