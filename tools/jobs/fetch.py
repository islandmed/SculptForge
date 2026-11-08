#!/usr/bin/env python3
"""sculptforge.app/jobs.html: current sculpting and 3D modeling roles, worldwide, from official public job feeds.

  fetch.py --state DIR [--site website]   fetch every source in studios.yaml once, update DIR (state.json, jobs.json,
                                          new.md, coverage.md), write website/jobs.json and website/jobs.html
  fetch.py --render [--site website]      website/jobs.html from website/jobs.json only (no network; the Pages deploy)
  fetch.py --validate DIR                 strict schema check of a run's files (the publish job, before pushing)
  fetch.py --self-test

Rules (owner 11-07, docs: BRIEF in changes/site-jobs.md):
- Only the feeds companies publish for embedding (studios.yaml lists them). robots.txt is read per host (RFC 9309:
  a 4xx robots.txt allows, a 5xx or unreachable one disallows for that run); one request per source per UTC day,
  User-Agent names us and a contact page.
- A role shows title (as published), studio, location (+ Remote), posted date and a link. Never the description:
  job ads name other apps' marks, and a title, studio or location with a mark is dropped (tools/check_marks.py).
- Roles posted (or, without a date, first seen) more than MAX_AGE days ago, or gone from their feed, drop off.
  A feed that fails keeps its last good roles, and coverage.md lists it as dead.
"""
import argparse
import datetime as dt
import email.utils
import html
import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
import urllib.robotparser
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
UA = "SculptForgeJobs/1.0 (+https://sculptforge.app/jobs.html)"
MAX_AGE = 90  # days (a role also leaves when it leaves its feed)
TIMEOUT = 30

FEEDS = {
    "greenhouse": "https://boards-api.greenhouse.io/v1/boards/{slug}/jobs",
    "lever": "https://api.lever.co/v0/postings/{slug}?mode=json",
    "lever-eu": "https://api.eu.lever.co/v0/postings/{slug}?mode=json",
    "ashby": "https://api.ashbyhq.com/posting-api/job-board/{slug}",
    "recruitee": "https://{slug}.recruitee.com/api/offers/",
    "personio": "https://{slug}.jobs.personio.de/xml",
    "teamtailor": "https://{slug}.teamtailor.com/jobs.rss",
}

REGIONS = ["North America", "Latin America", "UK/Ireland", "Europe", "Middle East/Africa", "India",
           "Greater China", "Japan", "Korea", "Southeast Asia", "Oceania"]
SECTORS = {
    "games": "Games", "film-vfx": "Film & VFX", "animation": "Animation", "collectibles": "Collectibles & toys",
    "3d-printing": "3D printing", "jewelry": "Jewelry", "medical-dental": "Medical & dental",
    "product-viz": "Product visualization", "ar-vr": "AR/VR & 3D tech", "advertising": "Advertising & CGI",
    "archviz": "Architectural visualization", "outsourcing": "Outsourcing",
}
ROLES = {"character": "Character & creature", "environment": "Environment", "prop": "Props & hard surface",
         "print": "Sculptor, figures & print", "cad": "Jewelry & dental CAD", "generalist": "3D artist & modeler"}

# --- Which titles are sculpting / 3D modeling roles. English + ja/zh/ko/es/pt/fr/de/pl. ---
INCLUDE = re.compile("|".join([
    r"sculpt", r"escult", r"rze[źz]b", r"bildhauer", r"digital sculptor",
    r"\b3d\b[^,;|]*\b(artist|art\b|modell?er|modell?ing|generalist|designer|character|environment|prop|asset|print)",
    r"\b(artist[ae]?|artiste|grafik|artysta|k[üu]nstler|designer)\b[^,;|]{0,15}\b3d\b",
    r"\b3d[- ](artist|k[üu]nstler|designer|druck|print)",
    r"\b(character|creature|environment|prop|hard[- ]?surface|vehicle|weapon|asset|outsourc\w*|world|level) artist",
    r"\bmodell?er\b", r"modelador", r"modeleu(r|se)", r"modellierer", r"modelarz",
    r"\b3d[- ]?print", r"impress[ãa]o 3d", r"impresi[óo]n 3d", r"impression 3d", r"druk 3d", r"\bstl\b",
    r"miniatur", r"figurine", r"\bfiguras\b",
    r"jewel+e?ry cad", r"\bcad\b[^,;|]{0,20}\b(designer|artist|modell?er|technician|sculpt)", r"dental (cad|design)",
    r"artista de (personajes|entornos|personagens|cen[áa]rios|props|ambientes)",
    r"artiste (personnage|environnement|props?)", r"grafik (postaci|[śs]rodowisk)",
    r"モデラー", r"造形", r"原型", r"スカルプ", r"フィギュア", r"3D ?(CG)?(デザイナー|アーティスト)", r"CGデザイナー",
    r"建模", r"模型师", r"雕刻", r"雕塑", r"手办", r"3D ?美术", r"3D ?美術",
    r"모델러", r"모델링", r"3D ?아티스트", r"조형", r"피규어",
]), re.I)
# Titles that match above but are not artist seats. HARD always drops; SOFT (leads of the craft) only when the
# title doesn't say sculpt ("Manager, Digital Sculpting" stays).
HARD = re.compile(r"\b(engineer\w*|developer|programmer|sales|account|recruit\w*|financ\w*|analyst|scientist|support|"
                  r"customer|counsel|legal|data|qa|tester|operations|ingénieur\w*|ingenier[oa]|engenheir[oa]|"
                  r"entwickler|programista|vertrieb|ventas|vendas|buyer|planner|operator|technical artist|tech artist)\b",
                  re.I)
SOFT = re.compile(r"\b(manager|director|producer|coordinator|outsource lead)\b", re.I)
# "3D print" alone is often a print-floor job (technician, production associate): it needs a design word too.
PRINT_ONLY = re.compile(r"print|impress|impresi|druk|druck", re.I)
PRINT_DESIGN = re.compile(r"design|artist|sculpt|model|\bcad\b|\bstl\b|miniatur|figur", re.I)
CONTRACT = re.compile(r"\b(contract\w*|freelance\w*|temp(orary)?|contrat|contrato|zlecenie|befristet|"
                      r"auto-?entrepreneur|part[- ]time)\b|契約|業務委託|계약|外包|兼职", re.I)
CAD_CONTEXT = re.compile(r"jewel|dental|crown|ring|dent|orthod", re.I)


def role_type(title):
    t = title.lower()
    if re.search(r"\bcad\b|jewel|dental|crown", t):
        return "cad"
    if re.search(r"print|druk|druck|impress|impresi|miniatur|figur|\bstl\b|\btoys?\b|造形|原型|フィギュア|手办|"
                 r"피규어|조형", t):
        return "print"
    if re.search(r"character|creature|personaj|personag|personnage|charakter|postaci|キャラ|角色|캐릭터", t):
        return "character"
    if re.search(r"environment|world|background|level art|level artist|entorno|cen[áa]rio|environnement|umgebung|[śs]rodowisk|"
                 r"背景|场景|場景|배경", t):
        return "environment"
    if re.search(r"\bprops?\b|hard[- ]?surface|vehicle|weapon|asset|accessor", t):
        return "prop"
    if re.search(r"sculpt|escult|rze[źz]b|bildhauer|雕|スカルプ", t):
        return "print"
    return "generalist"


def is_sculpt_role(title, sector):
    if not INCLUDE.search(title) or HARD.search(title):
        return False
    if SOFT.search(title) and not re.search(r"sculpt|escult|rze[źz]b|bildhauer|スカルプ|雕", title, re.I):
        return False
    if PRINT_ONLY.search(title) and not PRINT_DESIGN.search(title):
        return False
    # A plain "CAD designer" at a machining shop is mechanical CAD, not sculpting.
    if re.search(r"\bcad\b", title, re.I) and not (CAD_CONTEXT.search(title) or sector in ("jewelry", "medical-dental")):
        return bool(re.search(r"sculpt|\b3d\b[^,;|]*(artist|modell?er)", title, re.I))
    return True


# --- Where a role is: a country (or city) in its location -> region. ---
def _countries():
    table = {
        "North America": "united states|usa|u.s.|us|america|canada|mexico*",
        "Latin America": "mexico|méxico|brazil|brasil|argentina|chile|colombia|peru|perú|uruguay|paraguay|bolivia|"
                         "ecuador|venezuela|costa rica|guatemala|panama|panamá|puerto rico|dominican republic|el salvador|"
                         "honduras|nicaragua|cuba|jamaica|latam|latin america",
        "UK/Ireland": "united kingdom|uk|england|scotland|wales|northern ireland|great britain|ireland|éire",
        "Europe": "germany|deutschland|france|spain|españa|portugal|italy|italia|netherlands|belgium|luxembourg|"
                  "switzerland|austria|österreich|poland|polska|czech republic|czechia|slovakia|hungary|romania|"
                  "bulgaria|greece|croatia|serbia|slovenia|bosnia|montenegro|north macedonia|albania|ukraine|"
                  "belarus|lithuania|latvia|estonia|finland|sweden|norway|denmark|iceland|malta|cyprus|moldova|"
                  "armenia|azerbaijan|kazakhstan|europe|european union|emea|cet",
        "Middle East/Africa": "israel|turkey|türkiye|turkiye|united arab emirates|uae|saudi arabia|ksa|qatar|bahrain|"
                              "kuwait|oman|jordan|lebanon|egypt|morocco|tunisia|algeria|nigeria|kenya|ghana|"
                              "south africa|ethiopia|rwanda|uganda|tanzania|senegal|ivory coast|côte d'ivoire|"
                              "cameroon|iran|iraq|pakistan*|middle east|mena|africa",
        "India": "india|pakistan|bangladesh|sri lanka|nepal",
        "Greater China": "china|中国|hong kong|macau|macao|taiwan|台湾|台灣",
        "Japan": "japan|日本",
        "Korea": "korea|south korea|republic of korea|대한민국|한국",
        "Southeast Asia": "singapore|malaysia|indonesia|philippines|thailand|vietnam|viet nam|cambodia|myanmar|"
                          "laos|brunei|apac|asia",
        "Oceania": "australia|new zealand|aotearoa|fiji|anz",
    }
    out = {}
    for region, names in table.items():
        for n in names.split("|"):
            if not n.endswith("*"):
                out.setdefault(n, region)
    return out


COUNTRY = _countries()
CITY = {
    "North America": "new york|nyc|manhattan|brooklyn|los angeles|santa monica|san francisco|bay area|san mateo|"
                     "redwood city|menlo park|san jose|fremont|seattle|bellevue|kirkland|redmond|austin|dallas|"
                     "carrollton|houston|boston|waltham|billerica|cambridge, ma|chicago|denver|boulder|portland|"
                     "irvine|san diego|novato|san rafael|carlsbad|cary|raleigh|atlanta|miami|orlando|"
                     "salt lake city|phoenix|minneapolis|maple plain|washington|north bethesda|baltimore|hunt valley|"
                     "sparks|philadelphia|pittsburgh|andover|madison|toronto|montreal|montréal|vancouver|calgary|"
                     "edmonton|ottawa|quebec|québec|london, ontario|halifax|winnipeg|burnaby|mississauga|"
                     "california|texas|washington state|massachusetts|oregon|colorado|new jersey|north carolina|"
                     "illinois|maryland|ontario|british columbia",
    "Latin America": "são paulo|sao paulo|rio de janeiro|porto alegre|curitiba|belo horizonte|florianópolis|"
                     "florianopolis|recife|buenos aires|córdoba|santiago|bogotá|bogota|medellín|medellin|lima|"
                     "montevideo|mexico city|ciudad de méxico|guadalajara|monterrey",
    "UK/Ireland": "london|guildford|leamington|brighton|cambridge|manchester|liverpool|edinburgh|glasgow|dundee|"
                  "sheffield|leeds|bristol|bath|nottingham|derby|birmingham|newcastle|oxford|reading|belfast|"
                  "dublin|cork|galway|kilkenny|telford|warwick|guernsey",
    "Europe": "paris|lyon|montpellier|bordeaux|lille|marseille|berlin|hamburg|munich|münchen|frankfurt|cologne|köln|"
              "düsseldorf|stuttgart|leipzig|madrid|barcelona|valencia|málaga|malaga|lisbon|lisboa|porto|milan|rome|"
              "amsterdam|rotterdam|utrecht|brussels|ghent|gent|antwerp|zurich|zürich|geneva|vienna|wien|warsaw|"
              "warszawa|kraków|krakow|wrocław|wroclaw|katowice|łódź|lodz|poznań|gdańsk|prague|praha|brno|"
              "bratislava|budapest|bucharest|cluj|sofia|athens|belgrade|zagreb|ljubljana|kyiv|kiev|lviv|vilnius|"
              "riga|tallinn|helsinki|espoo|tampere|stockholm|nacka|malmö|malmo|gothenburg|göteborg|umeå|skövde|"
              "uppsala|oslo|copenhagen|københavn|aarhus|reykjavik|valletta|san ġiljan|san giljan|st julian|"
              "limassol|nicosia|tbilisi|yerevan",
    "Middle East/Africa": "tel aviv|herzliya|haifa|jerusalem|istanbul|ankara|izmir|i̇stanbul|dubai|abu dhabi|riyadh|"
                          "jeddah|doha|amman|beirut|cairo|casablanca|tunis|lagos|nairobi|accra|cape town|"
                          "johannesburg|kigali",
    "India": "bangalore|bengaluru|mumbai|pune|hyderabad|chennai|delhi|new delhi|gurgaon|gurugram|noida|kolkata|"
             "ahmedabad|kochi|jaipur|karachi|lahore|dhaka|colombo",
    "Greater China": "shanghai|beijing|shenzhen|guangzhou|hangzhou|chengdu|wuhan|nanjing|suzhou|xiamen|xi'an|"
                     "taipei|kaohsiung|上海|北京|深圳|广州|杭州|成都",
    "Japan": "tokyo|osaka|kyoto|fukuoka|sapporo|nagoya|yokohama|東京|大阪|福岡",
    "Korea": "seoul|pangyo|seongnam|busan|incheon|daejeon|서울|판교|성남",
    "Southeast Asia": "kuala lumpur|penang|cyberjaya|jakarta|bandung|yogyakarta|bali|manila|makati|cebu|bangkok|"
                      "chiang mai|ho chi minh|hanoi|da nang|saigon|phnom penh",
    "Oceania": "sydney|melbourne|brisbane|adelaide|perth|canberra|hobart|gold coast|auckland|wellington|"
               "christchurch|dunedin",
}
US_STATES = set("AL AK AZ AR CO CT DE FL GA HI ID IL IA KS KY LA ME MD MA MI MN MS MO MT NE NV NH NJ NM NY NC ND OH OK "
                "OR PA RI SC SD TN TX UT VT VA WA WV WI WY DC".split())
ISO2 = {"US": "North America", "CA": "North America", "MX": "Latin America", "BR": "Latin America",
        "AR": "Latin America", "CL": "Latin America", "CO": "Latin America", "UY": "Latin America",
        "GB": "UK/Ireland", "IE": "UK/Ireland", "DE": "Europe", "FR": "Europe", "ES": "Europe", "PT": "Europe",
        "IT": "Europe", "NL": "Europe", "BE": "Europe", "CH": "Europe", "AT": "Europe", "PL": "Europe",
        "CZ": "Europe", "HU": "Europe", "RO": "Europe", "SE": "Europe", "NO": "Europe", "DK": "Europe",
        "FI": "Europe", "MT": "Europe", "CY": "Europe", "UA": "Europe", "RS": "Europe", "IL": "Middle East/Africa",
        "TR": "Middle East/Africa", "AE": "Middle East/Africa", "SA": "Middle East/Africa", "ZA": "Middle East/Africa",
        "IN": "India", "CN": "Greater China", "HK": "Greater China", "TW": "Greater China", "JP": "Japan",
        "KR": "Korea", "SG": "Southeast Asia", "MY": "Southeast Asia", "ID": "Southeast Asia", "PH": "Southeast Asia",
        "TH": "Southeast Asia", "VN": "Southeast Asia", "AU": "Oceania", "NZ": "Oceania"}
_CITY_RE = {r: re.compile(r"(?<![\w])(" + names + r")(?![\w])", re.I) for r, names in CITY.items()}
_COUNTRY_RE = re.compile(r"(?<![\w])(" + "|".join(sorted(map(re.escape, COUNTRY), key=len, reverse=True)) +
                         r")(?![\w])", re.I)


def regions_of(location, countries=(), home=None):
    """Regions a location string names: country names, then cities, then ISO codes; else the studio's home."""
    found = []
    for wrong, right in AMBIGUOUS:
        location = re.sub(wrong, right, location, flags=re.I)
    for c in countries:
        c = (c or "").strip()
        r = ISO2.get(c.upper()) if len(c) == 2 else COUNTRY.get(c.lower())
        if r and r not in found:
            found.append(r)
    if not found:
        for m in _COUNTRY_RE.finditer(location):
            word = m.group(1)
            # "US" / "UK" as words count; "us" inside a sentence ("join us") does not.
            if word.lower() in ("us", "uk", "uae", "ksa", "anz", "cet", "emea", "apac", "mena") and not word.isupper():
                continue
            r = COUNTRY[word.lower()]
            if r not in found:
                found.append(r)
    if not found:
        for r, rx in _CITY_RE.items():
            if rx.search(location) and r not in found:
                found.append(r)
    if not found:
        for tok in re.findall(r"\b[A-Z]{2}\b", location):
            r = "North America" if tok in US_STATES else ISO2.get(tok)
            if r and r not in found:
                found.append(r)
                break
    if not found and home:
        found.append(home)
    return [r for r in REGIONS if r in found]


# Place names that contain another place's name ("Wales", "Mexico", "London").
AMBIGUOUS = [(r"new south wales", "Australia"), (r"new mexico", "United States"), (r"london,? ontario", "Ontario"),
             (r"georgia,? (us|usa|united states)|atlanta,? georgia", "Atlanta, United States")]
REMOTE = re.compile(r"\bremote\b|\bremoto\b|\btélétravail\b|\bzdaln|リモート|远程|원격|anywhere|worldwide", re.I)


# --- Feeds ---
def load_studios(path=HERE / "studios.yaml"):
    """studios.yaml: 'Region:' headers and '  - {key: value, ...}' lines (no YAML library needed)."""
    studios, region = [], None
    for n, line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        s = line.split(" #")[0].rstrip() if not line.lstrip().startswith("#") else ""
        if not s.strip():
            continue
        if not s.startswith(" ") and s.endswith(":"):
            region = s[:-1].strip()
            if region not in REGIONS:
                raise SystemExit(f"studios.yaml:{n}: unknown region {region!r}")
            continue
        m = re.fullmatch(r"\s+-\s*\{(.*)\}", s)
        if not m or not region:
            raise SystemExit(f"studios.yaml:{n}: expected '  - {{name: ..., ats: ..., slug: ..., sector: ...}}'")
        d = dict(kv.split(":", 1) for kv in re.split(r",\s*(?=\w+:)", m.group(1)))
        d = {k.strip(): v.strip() for k, v in d.items()}
        if d.get("ats") not in FEEDS or d.get("sector") not in SECTORS or not d.get("slug") or not d.get("name"):
            raise SystemExit(f"studios.yaml:{n}: needs name, ats ({'/'.join(FEEDS)}), slug, sector ({'/'.join(SECTORS)})")
        d["region"] = region
        d["key"] = f"{d['ats']}:{d['slug']}"
        studios.append(d)
    keys = [s["key"] for s in studios]
    dupes = {k for k in keys if keys.count(k) > 1}
    if dupes:
        raise SystemExit(f"studios.yaml: listed twice: {', '.join(sorted(dupes))}")
    return studios


_robots = {}


def allowed(url):
    """RFC 9309: robots.txt 2xx -> its rules; 4xx -> allowed; 5xx or no answer -> not this run."""
    p = urllib.parse.urlsplit(url)
    host = f"{p.scheme}://{p.netloc}"
    if host not in _robots:
        rp = urllib.robotparser.RobotFileParser()
        try:
            with urllib.request.urlopen(urllib.request.Request(host + "/robots.txt", headers={"User-Agent": UA}),
                                        timeout=TIMEOUT) as r:
                rp.parse(r.read().decode("utf-8", "replace").splitlines())
        except urllib.error.HTTPError as e:
            rp.allow_all = 400 <= e.code < 500
            rp.disallow_all = not rp.allow_all
        except Exception:
            rp.disallow_all = True
        _robots[host] = rp
    return _robots[host].can_fetch(UA, url)


def http_get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "*/*"}),
                                timeout=TIMEOUT) as r:
        return r.read()


def _date(s):
    """ISO date (YYYY-MM-DD) from an ISO timestamp, RFC 822 date, or epoch milliseconds."""
    if s is None or s == "":
        return None
    if isinstance(s, (int, float)):
        return dt.datetime.fromtimestamp(s / 1000, dt.timezone.utc).date().isoformat()
    s = str(s).strip()
    m = re.match(r"(\d{4}-\d{2}-\d{2})", s)
    if m:
        return m.group(1)
    try:
        return email.utils.parsedate_to_datetime(s).astimezone(dt.timezone.utc).date().isoformat()
    except (TypeError, ValueError):
        return None


def parse(ats, slug, raw):
    """Feed bytes -> [{id, title, location, countries, remote, posted, url, contract}]."""
    out = []
    if ats == "greenhouse":
        for j in json.loads(raw)["jobs"]:
            out.append(dict(id=j["id"], title=j["title"], location=(j.get("location") or {}).get("name") or "",
                            posted=_date(j.get("first_published") or j.get("updated_at")), url=j["absolute_url"]))
    elif ats.startswith("lever"):
        for j in json.loads(raw):
            c = j.get("categories") or {}
            out.append(dict(id=j["id"], title=j["text"], location=c.get("location") or "",
                            countries=[j.get("country")], remote=j.get("workplaceType") == "remote",
                            posted=_date(j.get("createdAt")), url=j["hostedUrl"],
                            contract=bool(CONTRACT.search(c.get("commitment") or ""))))
    elif ats == "ashby":
        for j in json.loads(raw)["jobs"]:
            if j.get("isListed") is False:
                continue
            addr = ((j.get("address") or {}).get("postalAddress") or {})
            locs = [j.get("location") or ""] + [x.get("location") or "" for x in j.get("secondaryLocations") or []]
            out.append(dict(id=j["id"], title=j["title"], location="; ".join(l for l in locs if l),
                            countries=[addr.get("addressCountry")], remote=bool(j.get("isRemote")),
                            posted=_date(j.get("publishedAt")), url=j["jobUrl"],
                            contract=j.get("employmentType") in ("Contract", "Temporary")))
    elif ats == "recruitee":
        for j in json.loads(raw)["offers"]:
            if j.get("status", "published") != "published":
                continue
            out.append(dict(id=j["id"], title=j["title"], location=j.get("location") or "",
                            countries=[j.get("country_code")], remote=bool(j.get("remote")),
                            posted=_date(j.get("published_at") or j.get("created_at")), url=j["careers_url"],
                            contract=j.get("employment_type_code") in ("contract", "freelance", "temporary")))
    elif ats == "personio":
        for p in ET.fromstring(raw).iter("position"):
            g = lambda k: (p.findtext(k) or "").strip()
            out.append(dict(id=g("id"), title=g("name"), location=g("office"), posted=_date(g("createdAt")),
                            url=f"https://{slug}.jobs.personio.de/job/{g('id')}",
                            contract=g("employmentType") in ("freelance", "temporary")
                            or bool(CONTRACT.search(g("recruitingCategory")))))
    elif ats == "teamtailor":
        ns = {"tt": "https://teamtailor.com/locations"}
        for it in ET.fromstring(raw).iter("item"):
            locs = it.findall("tt:locations/tt:location", ns)
            cities = [l.findtext("tt:city", "", ns) or l.findtext("tt:name", "", ns) for l in locs]
            countries = [l.findtext("tt:country", "", ns) for l in locs]
            where = "; ".join(", ".join(x for x in (c, k) if x) for c, k in zip(cities, countries))
            out.append(dict(id=it.findtext("guid") or it.findtext("link"), title=it.findtext("title") or "",
                            location=where, countries=countries,
                            remote=(it.findtext("remoteStatus") or "none") in ("fully", "remote"),
                            posted=_date(it.findtext("pubDate")), url=it.findtext("link") or ""))
    for r in out:
        r["id"] = str(r["id"])
        r["title"] = re.sub(r"\s+", " ", html.unescape(r["title"] or "")).strip()
        r["location"] = re.sub(r"\s+", " ", r["location"] or "").strip(" ,;")[:2000]
        r["countries"] = [c[:100] if isinstance(c, str) else None for c in r.get("countries") or []][:50]
        r.setdefault("remote", False)
        r.setdefault("contract", False)
    return [r for r in out if r["title"] and len(r["title"]) <= 1000 and HTTPS_URL.fullmatch(r["url"] or "")
            and len(r["id"]) <= 300]


def _marks():
    sys.path.insert(0, str(ROOT / "tools"))
    import check_marks  # noqa: E402  (the repo's own check; discord.py uses it the same way)
    mark_re = check_marks.load_marks()
    return lambda *texts: any(check_marks.marks_in(t, mark_re) for t in texts if t)


# --- One daily run ---
def run(state_dir, site, today=None, fetch=http_get, has_mark=None):
    today = today or dt.datetime.now(dt.timezone.utc).date().isoformat()
    has_mark = has_mark or _marks()
    state_dir = Path(state_dir)
    state_dir.mkdir(parents=True, exist_ok=True)
    sp = state_dir / "state.json"
    state = json.loads(sp.read_text()) if sp.exists() else {}
    sources, first_seen = state.get("sources", {}), state.get("first_seen", {})
    studios = load_studios()
    cutoff = (dt.date.fromisoformat(today) - dt.timedelta(days=MAX_AGE)).isoformat()
    roles, cov = [], {"sources": len(studios), "dead": [], "blocked": [], "marks_dropped": 0, "matched": 0}
    for s in studios:
        src = sources.setdefault(s["key"], {})
        url = FEEDS[s["ats"]].format(slug=urllib.parse.quote(s["slug"]))
        if src.get("fetched") != today:  # one request per source per UTC day, re-runs reuse it
            try:
                if not allowed(url):
                    cov["blocked"].append(s["name"])
                    raise RuntimeError("robots.txt disallows")
                src["roles"] = parse(s["ats"], s["slug"], fetch(url))
                src["ok"], src["error"] = today, None
            except Exception as e:  # keep the last good roles: one broken feed never empties the page
                src["error"] = f"{type(e).__name__}: {e}"[:200]
            src["fetched"] = today
        if src.get("error"):
            cov["dead"].append({"studio": s["name"], "feed": s["key"], "error": src["error"],
                                "last_ok": src.get("ok")})
        for r in src.get("roles") or []:
            if not is_sculpt_role(r["title"], s["sector"]):
                continue
            cov["matched"] += 1
            rid = f"{s['key']}:{r['id']}"
            seen = first_seen.setdefault(rid, today)
            posted = min(r.get("posted") or seen, today)
            if posted < cutoff:
                continue
            if has_mark(r["title"], s["name"], r["location"]):
                cov["marks_dropped"] += 1
                continue
            rt = role_type(r["title"])
            sector = s["sector"]
            if rt == "cad":
                sector = "jewelry" if re.search(r"jewel|ring", r["title"], re.I) else (
                    "medical-dental" if re.search(r"dent|orthod|crown", r["title"], re.I) else sector)
            # Feeds put placeholders or the company name where the place goes ("BLANK,BLANK,Multiple Locations").
            loc = ", ".join(x for x in (t.strip() for t in r["location"].split(",")) if x and x.upper() != "BLANK")
            if loc.lower() in (s["name"].lower(), s["slug"].lower()):
                loc = ""
            r = dict(r, location=loc)
            remote = bool(r["remote"] or REMOTE.search(r["location"]))
            roles.append({
                "id": rid, "title": r["title"], "studio": s["name"], "location": r["location"][:120],
                "regions": regions_of(r["location"], r["countries"], s["region"]), "remote": remote,
                "posted": posted, "url": r["url"], "sector": sector, "role": rt,
                "contract": bool(r["contract"] or CONTRACT.search(r["title"])), "new": seen == today,
            })
    # Forget roles gone from every feed (their first-seen date too) so state.json doesn't grow forever.
    live = {f"{k}:{r['id']}" for k, v in sources.items() for r in v.get("roles") or []}
    first_seen = {k: v for k, v in first_seen.items() if k in live}
    known = {s["key"] for s in studios}
    sources = {k: v for k, v in sources.items() if k in known}
    roles.sort(key=lambda r: (r["posted"], r["studio"], r["title"]), reverse=True)
    out = {"generated": today, "max_age_days": MAX_AGE, "regions": REGIONS, "sectors": SECTORS, "roles_types": ROLES,
           "studios": len(studios), "roles": roles}
    sp.write_text(json.dumps({"sources": sources, "first_seen": first_seen}, ensure_ascii=False, sort_keys=True))
    (state_dir / "jobs.json").write_text(json.dumps(out, ensure_ascii=False, indent=0))
    (state_dir / "new.md").write_text(digest(roles, today), encoding="utf-8")
    (state_dir / "coverage.md").write_text(coverage(studios, roles, cov, today), encoding="utf-8")
    if site:
        Path(site, "jobs.json").write_text(json.dumps(out, ensure_ascii=False, indent=0) + "\n", encoding="utf-8")
        render(site, has_mark)
    return out, cov


def digest(roles, today):
    """New roles today, for the #jobs Discord post (~/Work/.sf/jobs/post-digest.sh)."""
    new = [r for r in roles if r["new"]]
    if not new:
        return ""
    lines = [f"**New sculpting & 3D roles, {today}** ({len(new)}): <https://sculptforge.app/jobs.html>"]
    for i, r in enumerate(new):
        where = r["location"] or ", ".join(r["regions"])
        remote = ", Remote" if r["remote"] and "remote" not in where.lower() else ""
        line = f"- {r['title']} at {r['studio']} ({where}{remote}) <{r['url']}>"
        if sum(len(x) + 1 for x in lines) + len(line) > 1900:  # one Discord message (2000 characters)
            lines.append(f"...and {len(new) - i} more on the page.")
            break
        lines.append(line)
    return "\n".join(lines) + "\n"


def coverage(studios, roles, cov, today):
    home = {r: 0 for r in REGIONS}
    for s in studios:
        home[s["region"]] += 1
    per = {r: 0 for r in REGIONS}
    for j in roles:
        for r in j["regions"]:
            per[r] += 1
    out = [f"## Jobs coverage {today}", "",
           f"{len(roles)} roles shown (matched {cov['matched']} sculpting/3D titles, {cov['marks_dropped']} dropped "
           f"for a mark), {cov['sources']} sources, {len(cov['dead'])} dead.", "",
           "| Region | Studios based there | Roles there |", "|---|---:|---:|"]
    out += [f"| {r} | {home[r]}{' (below 8)' if home[r] < 8 else ''} | {per[r]} |" for r in REGIONS]
    if cov["dead"]:
        out += ["", "**Dead sources** (last good roles kept):", ""]
        out += [f"- {d['studio']} `{d['feed']}`: {d['error']} (last ok {d['last_ok'] or 'never'})" for d in cov["dead"]]
    if cov["blocked"]:
        out += ["", "robots.txt blocks: " + ", ".join(cov["blocked"])]
    return "\n".join(out) + "\n"


# --- The page ---
def render(site, has_mark=None):
    """Fill website/jobs.html's <!-- jobs:NAME --> ... <!-- /jobs:NAME --> blocks from website/jobs.json.

    The page is its own template: everything outside the blocks (layout, filters, script) is edited by hand."""
    site = Path(site)
    data = json.loads((site / "jobs.json").read_text(encoding="utf-8"))
    has_mark = has_mark or _marks()
    marked = [r["title"] for r in data["roles"] if has_mark(r["title"], r["studio"], r["location"])]
    if marked:  # fetch drops these; data from anywhere else still never reaches the page with one
        raise SystemExit(f"jobs.json: {len(marked)} roles with a mark, page not rendered")
    e = lambda s: html.escape(str(s), quote=True)
    rows = []
    for r in data["roles"]:
        if not HTTPS_URL.fullmatch(str(r.get("url", ""))):  # validate() refuses these too; never a javascript: link
            continue
        where = r["location"] or ", ".join(r["regions"])
        remote = ' <span class="tag">Remote</span>' if r["remote"] else ""
        rows.append(
            f'          <li class="job" data-regions="{e("|".join(r["regions"]))}" data-sector="{e(r["sector"])}" '
            f'data-role="{e(r["role"])}" data-remote="{int(r["remote"])}" data-contract="{int(r["contract"])}">'
            f'<a href="{e(r["url"])}" rel="noopener nofollow" target="_blank">'
            f'<span class="job-title">{e(r["title"])}</span>'
            f'<span class="job-studio">{e(r["studio"])}</span>'
            f'<span class="job-where">{e(where)}{remote}</span>'
            f'<time datetime="{e(r["posted"])}">{e(r["posted"])}</time></a></li>')
    def opts(key, labels):
        """Every option with its count; empty ones stay listed (the page's scope) but can't be picked."""
        out = []
        for v, label in labels:
            n = sum(v in r[key] if isinstance(r[key], list) else v == r[key] for r in data["roles"])
            out.append(f'<option value="{e(v)}"{"" if n else " disabled"}>{e(label)} ({n})</option>')
        return "".join(out)
    blocks = {
        "rows": "\n" + "\n".join(rows or ['          <li class="job-empty">No open roles right now.</li>']) + "\n",
        "count": str(len(data["roles"])),
        "studios": str(data["studios"]),
        "updated": e(data["generated"]),
        "maxage": str(data["max_age_days"]),
        "regions": opts("regions", [(x, x) for x in data["regions"]]),
        "sectors": opts("sector", list(data["sectors"].items())),
        "roles": opts("role", list(data["roles_types"].items())),
    }
    page = (site / "jobs.html").read_text(encoding="utf-8")
    for name, value in blocks.items():
        page, n = re.subn(rf"(<!-- jobs:{name} -->).*?(<!-- /jobs:{name} -->)",
                          lambda m: m.group(1) + value + m.group(2), page, flags=re.S)
        if n != 1:
            raise SystemExit(f"{site / 'jobs.html'}: expected one <!-- jobs:{name} --> block, found {n}")
    (site / "jobs.html").write_text(page, encoding="utf-8")


# --- The publish job's check: what the fetch job hands over (it parsed untrusted feeds) before a write token sees it ---
HTTPS_URL = re.compile(r"https://[^\s\x00-\x1f\x7f<>\"'`\\]{1,1000}")
DATE = re.compile(r"\d{4}-\d{2}-\d{2}")
ARTIFACT = {"state.json": 30_000_000, "jobs.json": 5_000_000, "new.md": 20_000, "coverage.md": 50_000}


def validate(folder):
    """Raise ValueError unless `folder` holds exactly the four files of a run, each within its schema and size."""
    folder = Path(folder)
    names = sorted(p.name for p in folder.iterdir())
    if names != sorted(ARTIFACT) or any(not (folder / n).is_file() or (folder / n).is_symlink() for n in names):
        raise ValueError(f"expected exactly {sorted(ARTIFACT)} as plain files, got {names}")
    text = {}
    for n, limit in ARTIFACT.items():
        raw = (folder / n).read_bytes()
        if len(raw) > limit:
            raise ValueError(f"{n}: {len(raw)} bytes, over {limit}")
        text[n] = raw.decode("utf-8")
        if re.search(r"[\x00-\x08\x0b-\x1f\x7f]", text[n]):
            raise ValueError(f"{n}: control characters")

    def need(cond, where):
        if not cond:
            raise ValueError(where)

    def string(v, where, lo=0, hi=300):
        need(isinstance(v, str) and lo <= len(v) <= hi and not re.search(r"[\x00-\x1f\x7f]", v), f"{where}: bad text")

    def date(v, where, optional=False):
        need((optional and v is None) or (isinstance(v, str) and DATE.fullmatch(v)), f"{where}: bad date {v!r}")

    def url(v, where):
        need(isinstance(v, str) and HTTPS_URL.fullmatch(v), f"{where}: not an https URL")

    def boolean(v, where):
        need(isinstance(v, bool), f"{where}: not true/false")

    jobs = json.loads(text["jobs.json"])
    need(isinstance(jobs, dict) and set(jobs) == {"generated", "max_age_days", "regions", "sectors", "roles_types",
                                                   "studios", "roles"}, "jobs.json: keys")
    date(jobs["generated"], "jobs.json generated")
    need(type(jobs["max_age_days"]) is int and 1 <= jobs["max_age_days"] <= 365, "jobs.json max_age_days")
    need(jobs["regions"] == REGIONS and jobs["sectors"] == SECTORS and jobs["roles_types"] == ROLES, "jobs.json labels")
    need(type(jobs["studios"]) is int and 0 <= jobs["studios"] <= 5000, "jobs.json studios")
    need(isinstance(jobs["roles"], list) and len(jobs["roles"]) <= 20000, "jobs.json roles")
    keys = {"id", "title", "studio", "location", "regions", "remote", "posted", "url", "sector", "role", "contract", "new"}
    for i, r in enumerate(jobs["roles"]):
        w = f"jobs.json roles[{i}]"
        need(isinstance(r, dict) and set(r) == keys, f"{w}: keys")
        string(r["id"], w + " id", 1, 400)
        string(r["title"], w + " title", 1, 300)
        string(r["studio"], w + " studio", 1, 120)
        string(r["location"], w + " location", 0, 120)
        need(isinstance(r["regions"], list) and r["regions"] and all(x in REGIONS for x in r["regions"]), w + " regions")
        date(r["posted"], w + " posted")
        url(r["url"], w + " url")
        need(r["sector"] in SECTORS and r["role"] in ROLES, w + " sector/role")
        for k in ("remote", "contract", "new"):
            boolean(r[k], f"{w} {k}")
    state = json.loads(text["state.json"])
    need(isinstance(state, dict) and set(state) == {"sources", "first_seen"}, "state.json: keys")
    need(isinstance(state["sources"], dict) and len(state["sources"]) <= 2000, "state.json sources")
    for key, src in state["sources"].items():
        w = f"state.json {key[:60]!r}"
        need(isinstance(key, str) and re.fullmatch(r"(%s):[A-Za-z0-9_.-]{1,80}" % "|".join(FEEDS), key), w + ": key")
        need(isinstance(src, dict) and set(src) <= {"roles", "ok", "error", "fetched"}, w + ": keys")
        date(src.get("ok"), w + " ok", optional=True)
        date(src.get("fetched"), w + " fetched", optional=True)
        need(src.get("error") is None or isinstance(src["error"], str) and len(src["error"]) <= 200, w + " error")
        roles = src.get("roles") or []
        need(isinstance(roles, list) and len(roles) <= 20000, w + " roles")
        for i, r in enumerate(roles):
            rw = f"{w} roles[{i}]"
            need(isinstance(r, dict) and set(r) == {"id", "title", "location", "countries", "remote", "posted", "url",
                                                    "contract"}, rw + ": keys")
            string(r["id"], rw + " id", 1, 300)
            string(r["title"], rw + " title", 1, 1000)
            string(r["location"], rw + " location", 0, 2000)
            need(isinstance(r["countries"], list) and len(r["countries"]) <= 50 and
                 all(c is None or isinstance(c, str) and len(c) <= 100 for c in r["countries"]), rw + " countries")
            date(r["posted"], rw + " posted", optional=True)
            url(r["url"], rw + " url")
            boolean(r["remote"], rw + " remote")
            boolean(r["contract"], rw + " contract")
    need(isinstance(state["first_seen"], dict) and len(state["first_seen"]) <= 200000, "state.json first_seen")
    for k, v in state["first_seen"].items():
        string(k, "state.json first_seen key", 1, 400)
        date(v, "state.json first_seen value")


def self_test():
    ok = lambda t, s="games": is_sculpt_role(t, s)
    for t in ["Senior Character Artist", "Environment Artist (Contract)", "3D Artist", "Prop Artist - ARC",
              "Hard Surface Artist", "Digital Sculptor", "3D Modeler", "Sculptor, Miniatures", "3D Print Designer",
              "Jewelry CAD Designer", "Dental CAD Designer", "Artista 3D", "Artiste personnage 3D", "Grafik 3D",
              "Modelador 3D", "3D 배경모델러 (인턴)", "3Dモデラー", "角色模型师", "Figurine Sculptor", "STL Artist",
              "Manager Digital Product Design (Digital Sculpting)", "3D Artist, Marketing", "Lead Level Artist",
              "Lead Character Artist"]:
        assert ok(t), t
    for t in ["Graphics Engineer", "Account Executive, 3D Printing", "Art Director", "Character Designer - Series",
              "Applications Engineer", "CAD Designer", "Financial Modeler", "Data Modeler", "Outsourcing Manager",
              "Lead Ad Monetisation Manager", "3D Printing Sales Specialist", "3D Print Technician",
              "3D Print Production Associate", "Assistente de Produção de Impressão 3D", "Senior Technical Artist",
              "Manager, 3D Character Art", "Environment Art Outsource Lead"]:
        assert not ok(t), t
    assert ok("CAD Designer", "jewelry")
    assert role_type("Senior Character Artist") == "character" and role_type("Jewelry CAD Designer") == "cad"
    assert role_type("Environment Artist") == "environment" and role_type("3D Artist") == "generalist"
    assert role_type("Miniatures Sculptor") == "print" and role_type("Character Sculptor") == "character"
    assert role_type("Digital Sculptor") == "print"
    R = regions_of
    assert R("Cary,North Carolina,United States") == ["North America"]
    assert R("IN - Bangalore, India") == ["India"]
    assert R("Canada-Remote; United Kingdom - Guildford Onsite; United States-Remote") == ["North America", "UK/Ireland"]
    assert R("Sarıyer, Istanbul") == ["Middle East/Africa"]
    assert R("Montevideo") == ["Latin America"] and R("Kuala Lumpur") == ["Southeast Asia"]
    assert R("Hybrid, SP", home="Latin America") == ["Latin America"]
    assert R("Remote - US") == ["North America"] and R("Join us anywhere", home="Europe") == ["Europe"]
    assert R("Sydney, New South Wales, Australia") == ["Oceania"] and R("London, Ontario or Remote") == ["North America"]
    assert R("Seattle, WA") == ["North America"] and R("", ["JP"]) == ["Japan"] and R("", ["Sweden"]) == ["Europe"]
    assert _date(1747304130972) == "2025-05-15" and _date("Fri, 30 Oct 2026 13:13:07 +0100") == "2026-10-30"
    assert _date("2026-11-03 17:18:23 UTC") == "2026-11-03"
    # A full run on canned feeds: one dead feed keeps its last roles, old and marked roles drop.
    import tempfile
    feeds = {"greenhouse": json.dumps({"jobs": [
        {"id": 1, "title": "Senior Character Artist", "location": {"name": "Montréal, Quebec, Canada"},
         "first_published": "2026-11-01T00:00:00Z", "absolute_url": "https://example.com/1"},
        {"id": 2, "title": "Environment Artist", "location": {"name": "Remote"},
         "first_published": "2026-01-01T00:00:00Z", "absolute_url": "https://example.com/2"},
        {"id": 3, "title": "Accountant", "location": {"name": "Paris"}, "absolute_url": "https://example.com/3"},
        {"id": 4, "title": "MARKED Prop Artist", "location": {"name": "Paris"}, "absolute_url": "https://example.com/4"},
    ]}).encode()}
    global load_studios, allowed
    real_load, real_allowed = load_studios, allowed
    load_studios = lambda: [dict(name="A", ats="greenhouse", slug="a", sector="games", region="Europe", key="greenhouse:a"),
                            dict(name="B", ats="greenhouse", slug="b", sector="games", region="Japan", key="greenhouse:b")]
    allowed = lambda url: True
    mark = lambda *t: any("MARKED" in x for x in t if x)
    try:
        with tempfile.TemporaryDirectory() as d:
            Path(d, "site").mkdir()
            Path(d, "site", "jobs.html").write_text((ROOT / "website" / "jobs.html").read_text(encoding="utf-8"))
            first = lambda url: feeds["greenhouse"]
            out, cov = run(Path(d, "state"), Path(d, "site"), "2026-11-07", first, mark)
            page = Path(d, "site", "jobs.html").read_text(encoding="utf-8")
            assert page.count('<li class="job" ') == 2 and "MARKED" not in page and ">Games (2)</option>" in page
            assert ' disabled>Jewelry (0)</option>' in page and "<!-- jobs:updated -->2026-11-07<" in page
            assert [r["title"] for r in out["roles"]] == ["Senior Character Artist"] * 2, out["roles"]
            assert cov["marks_dropped"] == 2 and out["roles"][0]["regions"] == ["North America"]
            def broken(url):
                if "/b/" in url:
                    raise OSError("down")
                return feeds["greenhouse"]
            out, cov = run(Path(d, "state"), None, "2026-11-08", broken, mark)
            assert len(out["roles"]) == 2 and cov["dead"] and cov["dead"][0]["last_ok"] == "2026-11-07"
            assert not any(r["new"] for r in out["roles"])
            # The publish job's check: a clean run passes; a javascript: link, an extra file or a bad type fails.
            validate(Path(d, "state"))
            def refused(edit):
                bad = Path(d, "bad")
                if bad.exists():
                    [p.unlink() for p in bad.iterdir()]
                else:
                    bad.mkdir()
                for n in ARTIFACT:
                    (bad / n).write_bytes(Path(d, "state", n).read_bytes())
                edit(bad)
                try:
                    validate(bad)
                except (ValueError, KeyError, TypeError, json.JSONDecodeError):
                    return True
                return False
            def jobs_edit(f):
                def edit(folder):
                    j = json.loads((folder / "jobs.json").read_text())
                    f(j)
                    (folder / "jobs.json").write_text(json.dumps(j))
                return edit
            assert refused(jobs_edit(lambda j: j["roles"][0].update(url="javascript:alert(1)")))
            assert refused(jobs_edit(lambda j: j["roles"][0].update(url="https://x.example/a b")))
            assert refused(jobs_edit(lambda j: j["roles"][0].update(remote="yes")))
            assert refused(jobs_edit(lambda j: j["roles"][0].update(extra=1)))
            assert refused(jobs_edit(lambda j: j["roles"][0].update(title="x" * 301)))
            assert refused(lambda f: (f / "evil.sh").write_text("x"))
            assert refused(lambda f: (f / "new.md").write_text("\x1b[31m"))
            # Hostile data that reaches the renderer anyway: escaped text, no non-https link.
            hostile = json.loads(Path(d, "site", "jobs.json").read_text())
            evil = dict(hostile["roles"][0], title='<script>alert("x")</script>\' onmouseover="y',
                        studio="<b>Evil & Co</b>", location='"><img src=x onerror=alert(1)>')
            hostile["roles"] = [evil, dict(evil, title="Click", url="javascript:alert(1)"),
                                dict(evil, title="Data", url="data:text/html,<script>alert(1)</script>")]
            Path(d, "site", "jobs.json").write_text(json.dumps(hostile))
            render(Path(d, "site"), mark)
            page = Path(d, "site", "jobs.html").read_text(encoding="utf-8")
            rows = page.split("<!-- jobs:rows -->")[1].split("<!-- /jobs:rows -->")[0]
            assert "<script" not in rows and "<img" not in rows and "<b>" not in rows and "onerror=alert" in rows
            assert "&lt;script&gt;alert(&quot;x&quot;)&lt;/script&gt;&#x27; onmouseover=&quot;y" in rows
            assert "javascript:" not in rows and "data:" not in rows and rows.count('<li class="job" ') == 1
            assert re.findall(r'href="([^"]*)"', rows) == [evil["url"]] and 'rel="noopener nofollow"' in rows
    finally:
        load_studios, allowed = real_load, real_allowed
    assert len(real_load()) >= 50
    print("fetch.py self-test: ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--state", help="directory with state.json (kept between runs); jobs.json, new.md, coverage.md")
    ap.add_argument("--site", default=str(ROOT / "website"))
    ap.add_argument("--render", action="store_true")
    ap.add_argument("--validate", metavar="DIR", help="schema-check a run's four files (the publish job)")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if a.render:
        return render(a.site)
    if a.validate:
        try:
            validate(a.validate)
        except (ValueError, KeyError, TypeError, json.JSONDecodeError, UnicodeDecodeError) as e:
            raise SystemExit(f"jobs data refused: {e}")
        return print(f"{a.validate}: valid")
    if not a.state:
        ap.error("--state DIR (or --render)")
    out, cov = run(a.state, a.site)
    print(coverage(load_studios(), out["roles"], cov, out["generated"]))


if __name__ == "__main__":
    main()
