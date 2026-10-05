#!/usr/bin/env python3
"""
pssg.py - Parser e visualizador para o formato Codemasters PSSG (EGO Engine).
Usa apenas a biblioteca padrao do Python 3.
"""

import sys
import os
import struct
import json
import zlib
from typing import Dict, List, Tuple, Any, Optional


class PSSGAttribute:
    def __init__(self, attr_id: int, name: str, raw_value: bytes, decoded_value: Any):
        self.attr_id = attr_id
        self.name = name
        self.raw_value = raw_value
        self.value = decoded_value

    def to_dict(self) -> Dict[str, Any]:
        val = self.value
        if isinstance(val, bytes):
            val = val.hex()
        return {
            "id": self.attr_id,
            "name": self.name,
            "value": val
        }


class PSSGNode:
    def __init__(self, node_id: int, type_name: str, offset: int, size: int, attr_size: int):
        self.node_id = node_id
        self.type_name = type_name
        self.offset = offset
        self.size = size
        self.attr_size = attr_size
        self.attributes: Dict[str, PSSGAttribute] = {}
        self.children: List['PSSGNode'] = []
        self.data: Optional[bytes] = None

    def get(self, attr_name: str, default=None):
        attr = self.attributes.get(attr_name)
        return attr.value if attr is not None else default

    @property
    def id(self) -> Optional[str]:
        return self.get("id")

    @property
    def nickname(self) -> Optional[str]:
        return self.get("nickname")

    def to_dict(self, include_data_hex: bool = False) -> Dict[str, Any]:
        d: Dict[str, Any] = {
            "type": self.type_name,
            "type_id": self.node_id,
            "offset": self.offset,
            "size": self.size,
            "attributes": {name: a.to_dict()["value"] for name, a in self.attributes.items()}
        }
        if self.children:
            d["children"] = [c.to_dict(include_data_hex) for c in self.children]
        elif self.data:
            d["data_length"] = len(self.data)
            if include_data_hex and len(self.data) <= 64:
                d["data_hex"] = self.data.hex()
        return d

    def dump_str(self, depth: int = 0, max_depth: Optional[int] = None) -> str:
        indent = "  " * depth
        attr_parts = []
        if self.nickname:
            attr_parts.append(f'nick="{self.nickname}"')
        if self.id:
            attr_parts.append(f'id="{self.id}"')
        for k, v in self.attributes.items():
            if k not in ("id", "nickname"):
                v_repr = repr(v.value if isinstance(v, PSSGAttribute) else v)
                if len(v_repr) > 40:
                    v_repr = v_repr[:37] + "...'"
                attr_parts.append(f"{k}={v_repr}")
        attrs_str = " " + " ".join(attr_parts) if attr_parts else ""
        data_str = f" [data: {len(self.data)} bytes]" if self.data else ""
        res = [f"{indent}[{self.offset:8d}] <{self.type_name}{attrs_str}>{data_str}"]

        if max_depth is None or depth < max_depth:
            for child in self.children:
                res.append(child.dump_str(depth + 1, max_depth))
        elif self.children:
            res.append(f"{indent}  ... ({len(self.children)} children omitted)")
        return "\n".join(res)


class PSSGFile:
    def __init__(self, filepath_or_data: Optional[Any] = None):
        self.filepath: Optional[str] = None
        self.file_size: int = 0
        self.attr_count: int = 0
        self.node_type_count: int = 0
        self.node_types: Dict[int, Tuple[str, Dict[int, str]]] = {}
        self.attr_names: Dict[int, str] = {}
        self.root: Optional[PSSGNode] = None

        if filepath_or_data:
            if isinstance(filepath_or_data, str) or hasattr(filepath_or_data, "read"):
                self.load(filepath_or_data)
            elif isinstance(filepath_or_data, (bytes, bytearray, memoryview)):
                self.parse(bytes(filepath_or_data))

    def load(self, source: Any) -> 'PSSGFile':
        if isinstance(source, str):
            self.filepath = source
            with open(source, "rb") as f:
                data = f.read()
        else:
            data = source.read()
        return self.parse(data)

    def parse(self, data: bytes) -> 'PSSGFile':
        if len(data) < 16:
            raise ValueError("Buffer muito pequeno para arquivo PSSG")

        magic, declared_size, self.attr_count, self.node_type_count = struct.unpack_from(">4sIII", data, 0)
        if magic != b"PSSG":
            raise ValueError(f"Assinatura invalida: esperava 'PSSG', obteve {magic!r}")

        self.file_size = declared_size

        # 1. Esquema
        pos = 16
        self.node_types.clear()
        self.attr_names.clear()

        for _ in range(self.node_type_count):
            if pos + 8 > len(data):
                break
            node_id, name_len = struct.unpack_from(">II", data, pos)
            pos += 8
            node_name = data[pos:pos+name_len].decode("ascii", errors="replace")
            pos += name_len
            attr_count, = struct.unpack_from(">I", data, pos)
            pos += 4
            attrs: Dict[int, str] = {}
            for _ in range(attr_count):
                aid, alen = struct.unpack_from(">II", data, pos)
                pos += 8
                aname = data[pos:pos+alen].decode("ascii", errors="replace")
                pos += alen
                attrs[aid] = aname
                self.attr_names[aid] = aname
            self.node_types[node_id] = (node_name, attrs)

        schema_end = pos

        # 2. Arvore de nos
        if schema_end < len(data):
            self.root = self._parse_node(data, schema_end, len(data))

        return self

    def _decode_attribute_value(self, aid: int, raw: bytes) -> Any:
        alen = len(raw)
        if alen >= 4:
            slen, = struct.unpack_from(">I", raw, 0)
            if slen == alen - 4 and slen > 0:
                try:
                    return raw[4:].decode("utf-8")
                except UnicodeDecodeError:
                    try:
                        return raw[4:].decode("latin1")
                    except Exception:
                        pass
        if alen == 4:
            uint_val, = struct.unpack(">I", raw)
            # Se parecer float razoavel
            float_val, = struct.unpack(">f", raw)
            if 0.0001 <= abs(float_val) <= 1000000.0:
                # Retorna int se for inteiro exato ou flag
                if uint_val in (0, 1, 2, 3, 4, 5, 4294967295):
                    return uint_val
                return float_val
            return uint_val
        elif alen == 1:
            return raw[0]
        elif alen == 2:
            return struct.unpack(">H", raw)[0]
        return raw

    def _parse_node(self, data: bytes, offset: int, max_end: int) -> Optional[PSSGNode]:
        if offset + 12 > max_end:
            return None

        nid, nsz, attr_len = struct.unpack_from(">III", data, offset)
        type_name = self.node_types.get(nid, (f"UNKNOWN_{nid}", {}))[0]
        node = PSSGNode(nid, type_name, offset, nsz, attr_len)

        # Parse atributos
        p = offset + 12
        end_attrs = min(max_end, p + attr_len)
        while p + 8 <= end_attrs:
            aid, alen = struct.unpack_from(">II", data, p)
            aname = self.attr_names.get(aid, f"attr_{aid}")
            val_bytes = data[p+8:min(end_attrs, p+8+alen)]
            val = self._decode_attribute_value(aid, val_bytes)
            node.attributes[aname] = PSSGAttribute(aid, aname, val_bytes, val)
            p += 8 + alen

        # Parse filhos ou payload
        payload_start = end_attrs
        payload_end = min(max_end, offset + 8 + nsz)
        payload_len = payload_end - payload_start

        if payload_len >= 12:
            # Testa se o payload se decompoe exatamente em nos validos
            test_pos = payload_start
            valid = True
            while test_pos < payload_end:
                if test_pos + 12 > payload_end:
                    valid = False
                    break
                cid, csz, cattr = struct.unpack_from(">III", data, test_pos)
                if cid not in self.node_types or cattr > csz or test_pos + 8 + csz > payload_end:
                    valid = False
                    break
                test_pos += 8 + csz

            if valid and test_pos == payload_end:
                curr = payload_start
                while curr < payload_end:
                    cid, csz, cattr = struct.unpack_from(">III", data, curr)
                    child = self._parse_node(data, curr, payload_end)
                    if child:
                        node.children.append(child)
                    curr += 8 + csz
                return node

        if payload_len > 0:
            raw_data = data[payload_start:payload_end]
            # Verifica se ha zlib embutido
            if len(raw_data) >= 6 and raw_data[:2] in (b'\x78\x9c', b'\x78\x01', b'\x78\xda', b'\x78\x5e'):
                try:
                    node.data = zlib.decompress(raw_data)
                except Exception:
                    node.data = raw_data
            else:
                node.data = raw_data

        return node

    def find_nodes(self, predicate) -> List[PSSGNode]:
        results: List[PSSGNode] = []
        def _walk(n: PSSGNode):
            if predicate(n):
                results.append(n)
            for ch in n.children:
                _walk(ch)
        if self.root:
            _walk(self.root)
        return results

    def find_by_type(self, type_name: str) -> List[PSSGNode]:
        return self.find_nodes(lambda n: n.type_name == type_name)

    def find_by_nickname(self, nickname: str) -> List[PSSGNode]:
        return self.find_nodes(lambda n: n.nickname == nickname)

    def find_by_id(self, id_val: str) -> List[PSSGNode]:
        return self.find_nodes(lambda n: n.id == id_val)

    def to_dict(self, include_data_hex: bool = False) -> Dict[str, Any]:
        return {
            "file_size": self.file_size,
            "attr_count": self.attr_count,
            "node_type_count": self.node_type_count,
            "root": self.root.to_dict(include_data_hex) if self.root else None
        }

    def to_json(self, indent: int = 2, include_data_hex: bool = False) -> str:
        return json.dumps(self.to_dict(include_data_hex), indent=indent, ensure_ascii=False)

    def dump_tree(self, max_depth: Optional[int] = None) -> str:
        if not self.root:
            return "<Arvore vazia>"
        return self.root.dump_str(0, max_depth)


def main():
    if len(sys.argv) < 2:
        print(f"Uso: {sys.argv[0]} <arquivo.pssg> [--dump [max_depth]] [--json] [--find-type TIPO] [--find-id ID] [--find-nick NICK]")
        sys.exit(1)

    filepath = sys.argv[1]
    pssg = PSSGFile(filepath)
    print(f"Arquivo carregado: {filepath}")
    print(f"  Tamanho declarado: {pssg.file_size} bytes")
    print(f"  Tipos de nós no esquema: {pssg.node_type_count}")
    print(f"  Atributos no esquema: {pssg.attr_count}")
    if pssg.root:
        print(f"  Raiz: {pssg.root.type_name} (filhos diretos: {len(pssg.root.children)})")

    i = 2
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg == "--dump":
            max_depth = None
            if i + 1 < len(sys.argv) and sys.argv[i+1].isdigit():
                max_depth = int(sys.argv[i+1])
                i += 1
            print(pssg.dump_tree(max_depth))
        elif arg == "--json":
            print(pssg.to_json())
        elif arg == "--find-type" and i + 1 < len(sys.argv):
            t = sys.argv[i+1]
            matches = pssg.find_by_type(t)
            print(f"Encontrados {len(matches)} nós do tipo '{t}':")
            for m in matches[:20]:
                print(f"  [{m.offset:8d}] id={m.id!r} nick={m.nickname!r} attrs={list(m.attributes.keys())}")
            if len(matches) > 20:
                print(f"  ... ({len(matches)-20} adicionais omitidos)")
            i += 1
        elif arg == "--find-id" and i + 1 < len(sys.argv):
            id_val = sys.argv[i+1]
            matches = pssg.find_by_id(id_val)
            print(f"Encontrados {len(matches)} nós com id '{id_val}':")
            for m in matches:
                print(f"  [{m.offset:8d}] type={m.type_name} nick={m.nickname!r} attrs={m.attributes}")
            i += 1
        elif arg == "--find-nick" and i + 1 < len(sys.argv):
            nick_val = sys.argv[i+1]
            matches = pssg.find_by_nickname(nick_val)
            print(f"Encontrados {len(matches)} nós com nickname '{nick_val}':")
            for m in matches:
                print(f"  [{m.offset:8d}] type={m.type_name} id={m.id!r} attrs={list(m.attributes.keys())}")
            i += 1
        i += 1


if __name__ == "__main__":
    main()
