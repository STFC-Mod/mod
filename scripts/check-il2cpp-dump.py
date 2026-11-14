#!/usr/bin/env python3
"""Check the IL2CPP bindings in mods/src against an Il2CppDumper dump.cs.

  check-il2cpp-dump.py check <dump.cs> [--old <dump.cs>]
      Every binding class, member and hook target used in the source must exist in the dump with
      matching static-ness and argument count. With --old, also lists used members whose
      declaration changed between the two dumps.

  check-il2cpp-dump.py diff <old dump.cs> <new dump.cs>
      Full member diff of every class the source binds, to spot drift the bindings do not use yet.

Produce dump.cs with Il2CppDumper from GameAssembly and global-metadata.dat of the client you
target, and keep the il2cpp.h it writes next to dump.cs: base classes are resolved from it.
This is a static text match: it does not check C++ types the way the runtime does, so
a clean run is necessary but not sufficient. Members reached only through unbound wrappers
(Widget<T>, List<T>, ...) are not checked.
"""
import argparse
import difflib
import os
import re
from collections import defaultdict

SRC_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mods', 'src')

TYPE_HEAD = re.compile(
    r'^(?:\[.*\]\s*)?(?:(?:public|private|protected|internal|static|sealed|abstract|readonly|ref|unsafe|partial)\s+)*'
    r'(class|struct|interface|enum)\s+(\S+?)(?:<[^>]*>)?(?:\s*:\s*([^/]+?))?\s*//\s*TypeDefIndex')
FIELD = re.compile(r'^(?:(?:public|private|protected|internal|static|readonly|const|new|volatile)\s+)*(.+?)\s+([\w<>`.$]+);\s*//')
PROPERTY = re.compile(
    r'^((?:(?:public|private|protected|internal|static|virtual|override|abstract|sealed|new)\s+)*)(.+?)\s+([\w.<>`]+)'
    r'\s*\{\s*(get;)?\s*(set;)?\s*\}')
# Abstract and extern methods end in ';' instead of '{ }'.
METHOD = re.compile(
    r'^((?:(?:public|private|protected|internal|static|virtual|override|abstract|sealed|extern|new|async|unsafe)\s+)*)'
    r'(.+?)\s+([\w.<>`$|]+?)(<[^(]*>)?\((.*)\)\s*[{;]')


def split_params(params):
    depth, out, buf = 0, [], ''
    for ch in params:
        if ch in '<([':
            depth += 1
        elif ch in '>)]':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(buf)
            buf = ''
        else:
            buf += ch
    out.append(buf)
    return out


class Dump:
    """Classes from dump.cs, keyed by (namespace, name). Nested types are named Outer.Inner."""

    def __init__(self, path):
        self.classes = {}
        self.by_name = defaultdict(list)
        # dump.cs names bases without their namespace. il2cpp.h, written next to it, spells out
        # each concrete class's base with namespace (and generic arguments) mangled into one name.
        self.bases = {}
        header = os.path.join(os.path.dirname(path), 'il2cpp.h')
        if os.path.exists(header):
            for line in open(header, encoding='utf-8', errors='replace'):
                if m := re.match(r'struct (\w+)_Fields : (\w+)_Fields \{', line):
                    self.bases[m.group(1)] = m.group(2)
        namespace, cur = None, None
        for line in open(path, encoding='utf-8', errors='replace'):
            if line.startswith('// Namespace:'):
                namespace = line[len('// Namespace:'):].strip()
                continue
            m = TYPE_HEAD.match(line)
            if m:
                raw_base = m.group(3).split(',')[0].strip() if m.group(3) else None
                cur = {'ns': namespace, 'name': m.group(2), 'base': raw_base and re.sub(r'<.*', '', raw_base),
                       'generic': '<' in line.split('//')[0].split(':')[0], 'generic_base': bool(raw_base and '<' in raw_base),
                       'fields': {}, 'props': {}, 'methods': defaultdict(list)}
                self.classes.setdefault((namespace, cur['name']), cur)
                self.by_name[cur['name']].append(cur)
                continue
            s = line.strip()
            if cur is None or not s or s.startswith(('//', '[')):
                continue
            decl = s.split('//')[0].strip()
            if (fm := FIELD.match(s)) and '(' not in decl:
                head = decl[:decl.rfind(fm.group(2))]
                cur['fields'][fm.group(2)] = {'static': bool(re.search(r'\b(static|const)\b', head)), 'decl': decl}
            elif pm := PROPERTY.match(s):
                cur['props'][pm.group(3).split('.')[-1]] = {'static': 'static' in pm.group(1), 'decl': s}
            elif mm := METHOD.match(s):
                name = mm.group(3) if mm.group(3) in ('.ctor', '.cctor') else mm.group(3).split('.')[-1]
                params = mm.group(5).strip()
                cur['methods'][name].append({'static': 'static' in mm.group(1), 'generic': bool(mm.group(4)),
                                             'arity': len(split_params(params)) if params else 0,
                                             'decl': s.rstrip(' {};')})

    def find(self, namespace, name):
        name = name.replace('/', '.')
        if (namespace, name) in self.classes:
            return self.classes[(namespace, name)]
        # Nested types are listed under their own (often empty) namespace.
        if '.' in name and self.by_name.get(name):
            return self.by_name[name][0]
        return None

    @staticmethod
    def mangle(c):
        return re.sub(r'\W', '_', f"{c['ns']}.{c['name']}" if c['ns'] else c['name'])

    def base_of(self, c):
        if not c['base']:
            return None
        candidates = self.by_name.get(c['base'].split('.')[-1], []) + self.by_name.get(c['base'], [])
        # ViewController and ViewController<T> share a name; the base's own generic-ness picks one.
        candidates = [b for b in candidates if b['generic'] == c['generic_base']] or candidates
        if mangled := self.bases.get(self.mangle(c)):
            exact = [b for b in candidates
                     if mangled == self.mangle(b) or mangled.startswith(self.mangle(b) + '_')]
            if exact:
                return max(exact, key=lambda b: len(self.mangle(b)))
        # Open generic classes have no il2cpp.h entry; fall back to the same namespace.
        same = [b for b in candidates if b['ns'] == c['ns']]
        return (same or candidates or [None])[0]

    def lookup(self, cls, kind, member):
        table = {'field': 'fields', 'prop': 'props', 'method': 'methods'}[kind]
        for _ in range(64):
            if cls is None:
                return None
            if member in cls[table]:
                return cls[table][member]
            cls = self.base_of(cls)
        return None


CLASS = r'il2cpp::Class<\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*>'
USE = re.compile(r'(?<![\w:])(Class::)?(Get|Set|Field|SetField|Call)<\s*"([^"]+)"')
HOOK = re.compile(r'(?<![\w:])(\w+)::(?:Class::)?Method\(\s*"([^"]+)"\s*(?:,\s*(\d+|\{[^}]*\}))?\)')
KIND = {'Get': 'prop', 'Set': 'prop', 'Field': 'field', 'SetField': 'field', 'Call': 'method'}


def call_arity(text):
    """Counts the arguments of the call that starts right after the template argument list."""
    close = text.find('>(')
    if close == -1:
        return None
    i, depth, args = close + 2, 1, ''
    while i < len(text) and depth:
        depth += {'(': 1, ')': -1}.get(text[i], 0)
        if depth:
            args += text[i]
        i += 1
    return len(split_params(args)) if args.strip() else 0


def scan_source(root):
    """Returns ({binding name: (asm, ns, class)}, [(file, owner, kind, member, static, arity)])."""
    classes, uses = {}, []
    for dirpath, _, files in os.walk(root):
        if os.sep + 'proto' in dirpath:
            continue
        for f in files:
            if not f.endswith(('.h', '.cc', '.mm')):
                continue
            path = os.path.join(dirpath, f)
            text = open(path, encoding='utf-8', errors='replace').read()
            for m in re.finditer(r'using\s+(\w+)\s*=\s*' + CLASS, text):
                classes.setdefault(m.group(1), m.group(2, 3, 4))
            for m in re.finditer(r'^\s*struct\s+(\w+)[^;{]*\{', text, re.M):
                i, depth = m.end(), 1
                while i < len(text) and depth:
                    depth += {'{': 1, '}': -1}.get(text[i], 0)
                    i += 1
                # Nested structs name their own Class; keep their members out of the outer binding.
                body = re.sub(r'struct\s+\w+[^;{]*\{(?:[^{}]|\{[^{}]*\})*\};', '', text[m.end():i])
                cm = re.search(r'using\s+Class\s*=\s*' + CLASS, body)
                if not cm:
                    continue
                classes.setdefault(m.group(1), cm.group(1, 2, 3))
                for u in USE.finditer(body):
                    kind = KIND[u.group(2)]
                    arity = call_arity(body[u.end():]) if kind == 'method' else None
                    uses.append((path, m.group(1), kind, u.group(3), bool(u.group(1)), arity))
                for h in HOOK.finditer(body):
                    if h.group(1) == 'Class':
                        uses.append((path, m.group(1), 'hook', h.group(2), None, h.group(3)))
            for h in HOOK.finditer(text):
                if h.group(1) != 'Class':
                    uses.append((path, h.group(1), 'hook', h.group(2), None, h.group(3)))
    return classes, uses


def check(args):
    dump = Dump(args.dump)
    old = Dump(args.old) if args.old else None
    bound, uses = scan_source(SRC_ROOT)
    problems, changes = set(), set()
    for path, owner, kind, member, static, arity in uses:
        if owner not in bound:
            continue
        asm, ns, name = bound[owner]
        where = f"{os.path.relpath(path, SRC_ROOT)}: {owner}"
        cls = dump.find(ns, name)
        if not cls:
            problems.add(f"{where}: class {ns}.{name} ({asm}) not found")
            continue
        info = dump.lookup(cls, 'method' if kind == 'hook' else kind, member)
        if info is None:
            problems.add(f"{where}: {kind} {member} not found on {ns}.{name} or its bases")
            continue
        if kind == 'hook':
            if arity and arity.isdigit() and not any(m['arity'] == int(arity) for m in info):
                problems.add(f"{where}: hook {member} has no {arity}-argument overload: "
                             + ' | '.join(m['decl'] for m in info))
            continue
        if kind == 'method':
            candidates = [m for m in info if not m['generic'] and (arity is None or m['arity'] == arity)]
            if not candidates:
                problems.add(f"{where}: Call<\"{member}\"> with {arity} arguments matches nothing: "
                             + ' | '.join(m['decl'] for m in info))
                continue
            if not any(m['static'] == static for m in candidates):
                problems.add(f"{where}: Call<\"{member}\"> static={static} does not match: "
                             + ' | '.join(m['decl'] for m in candidates))
            decls = sorted(m['decl'] for m in candidates)
        else:
            if info['static'] != static:
                problems.add(f"{where}: {kind} {member} static={static} does not match: {info['decl']}")
            decls = [info['decl']]
        if old and (old_cls := old.find(ns, name)):
            old_info = old.lookup(old_cls, kind, member)
            if old_info is None:
                continue
            if kind == 'method':
                old_decls = sorted(m['decl'] for m in old_info if not m['generic'] and (arity is None or m['arity'] == arity))
            else:
                old_decls = [old_info['decl']]
            if old_decls != decls:
                changes.add(f"{where}: {ns}.{name}.{member}: {' | '.join(old_decls)}  ->  {' | '.join(decls)}")

    print(f"{len(bound)} bindings, {len(uses)} member uses")
    print(f"\n== problems ({len(problems)})")
    print('\n'.join(sorted(problems)))
    if old:
        print(f"\n== used declarations changed since the old dump ({len(changes)})")
        print('\n'.join(sorted(changes)))


def class_blocks(path):
    """Member lines per (namespace, class), without RVA/offset comments that change every build."""
    out, namespace, cur, buf = {}, None, None, []
    for line in open(path, encoding='utf-8', errors='replace'):
        if line.startswith('// Namespace:'):
            namespace = line[len('// Namespace:'):].strip()
            continue
        m = TYPE_HEAD.match(line)
        if m:
            if cur:
                out.setdefault(cur, buf)
            cur, buf = (namespace, m.group(2)), []
            continue
        s = line.strip()
        if cur and s and not s.startswith(('//', '/*', '|', '*/', '[')):
            buf.append(re.sub(r'\s*//.*', '', s))
    if cur:
        out.setdefault(cur, buf)
    return out


def diff(args):
    old, new = class_blocks(args.old), class_blocks(args.new)
    bound, _ = scan_source(SRC_ROOT)
    changed = 0
    for ns, name in sorted({(ns, name.replace('/', '.')) for _, ns, name in bound.values()}):
        key = (ns, name)
        if key not in old and key not in new:
            key = next((k for k in {**old, **new} if k[1] == name), key)
        before, after = old.get(key), new.get(key)
        if before is None or after is None:
            print(f"!! {ns}.{name}: in old dump={before is not None}, in new dump={after is not None}")
            continue
        if before != after:
            changed += 1
            print(f"== {ns}.{name}")
            for line in difflib.unified_diff(before, after, lineterm='', n=0):
                if not line.startswith(('---', '+++', '@@')):
                    print('   ' + line)
    print(f"{len(bound)} bindings, {changed} bound classes changed")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = parser.add_subparsers(required=True)
    p = sub.add_parser('check', help='check bindings against a dump')
    p.add_argument('dump')
    p.add_argument('--old', help='previous dump.cs, to list used declarations that changed')
    p.set_defaults(run=check)
    p = sub.add_parser('diff', help='diff bound classes between two dumps')
    p.add_argument('old')
    p.add_argument('new')
    p.set_defaults(run=diff)
    args = parser.parse_args()
    args.run(args)


if __name__ == '__main__':
    main()
