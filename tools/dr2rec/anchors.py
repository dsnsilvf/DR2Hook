"""Âncoras já confirmadas. O analyzer não reabre o significado delas.

Os endereços vêm de SSOT.md e de docs/reverse_engineering/
(suspensão, setup e pneus). O `+0x000` citado como Up do bloco da roda é o
cabeçalho em `rig + 0x1680 + i*0x420`, não o byte zero do PhysicsRig. O Up do
chassi está em `+0x300`.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

WHEEL_ORDER = ("RL", "RR", "FL", "FR")
WHEEL_STRIDE = 0x420
WHEEL_BLOCK_BASE = 0x1480
WHEEL_HEADER_BASE = 0x1680

# Telemetria UDP já usada para fechar a âncora de suspensão. Não é descoberta.
UDP_SUSPENSION = (68, 72, 76, 80)  # RL, RR, FL, FR


@dataclass(frozen=True)
class Anchor:
    name: str
    offset: int
    size: int
    kind: str
    confidence: str
    note: str
    wheel: str | None = None

    def to_meta(self) -> dict:
        row = asdict(self)
        row["offset"] = self.offset
        return row


def confirmed_anchors() -> list[Anchor]:
    """Campos cujo significado básico já está fechado."""
    from tools.dr2rec.confidence import CONFIRMED

    items = [
        Anchor(
            "chassis_quaternion",
            0x2E0,
            16,
            "vec4",
            CONFIRMED,
            "Quatérnion do chassi (x, y, z, w).",
        ),
        Anchor(
            "chassis_right",
            0x2F0,
            16,
            "vec3",
            CONFIRMED,
            "Eixo Right do chassi, no mundo.",
        ),
        Anchor(
            "chassis_up",
            0x300,
            16,
            "vec3",
            CONFIRMED,
            "Eixo Up do chassi, no mundo.",
        ),
        Anchor(
            "chassis_forward",
            0x310,
            16,
            "vec3",
            CONFIRMED,
            "Eixo Forward do chassi, no mundo.",
        ),
    ]
    per_wheel = (
        (
            "suspension_position",
            0x1504,
            4,
            "f32",
            "Escalar de curso. suspension_position UDP = float32(valor * 1000).",
        ),
        (
            "axle_point_local",
            0x1480,
            16,
            "vec3",
            "Ponto do eixo no referencial do chassi. X negado na roda direita.",
        ),
        (
            "setup_vertical",
            0x14E8,
            4,
            "f32",
            "Escalar vertical de setup subtraído do Y local.",
        ),
        (
            "geom_with_suspension",
            0x1660,
            16,
            "vec3",
            "Ponto geométrico com o deslocamento de suspensão. Não é posição de mundo.",
        ),
        (
            "geom_without_suspension",
            0x1670,
            16,
            "vec3",
            "O mesmo ponto sem o termo [+0x1504] * Up.",
        ),
        (
            "wheel_up",
            0x1680,
            16,
            "vec3",
            "Up compartilhado, no +0x00 do cabeçalho da roda (rig+0x1680+i*0x420).",
        ),
    )
    for index, wheel in enumerate(WHEEL_ORDER):
        for name, offset, size, kind, note in per_wheel:
            items.append(
                Anchor(
                    f"{name}_{wheel}",
                    offset + index * WHEEL_STRIDE,
                    size,
                    kind,
                    CONFIRMED,
                    note,
                    wheel,
                )
            )
    return items


def anchor_blob_size(anchors: list[Anchor] | None = None) -> int:
    return sum(item.size for item in (anchors or confirmed_anchors()))


# Campos já escritos no SSOT. Servem para não reapresentá-los como achado novo.
DOCUMENTED: tuple[tuple[int, int, str], ...] = (
    (0x2D0, 16, "posição do centro de massa"),
    (0x2E0, 16, "quatérnion do chassi"),
    (0x2F0, 16, "Right do chassi"),
    (0x300, 16, "Up do chassi"),
    (0x310, 16, "Forward do chassi"),
    (0x320, 16, "velocidade linear"),
    (0x330, 16, "velocidade angular"),
    (0x8E8, 4, "especificação de marcha lenta, RPM"),
    (0x8F4, 4, "quantidade de marchas à frente, float"),
    (0x918, 4, "rotação de potência máxima, RPM"),
    (0x12C0, 8, "ponteiro do rig para si, via subobjeto +0x120"),
    (0x12D0, 4, "contagem de rodas, valor confirmado 4"),
    (0x13D8, 4, "velocidade angular do virabrequim, rad/s"),
    (0x1400, 4, "número de marchas à frente, int32"),
    (0x140C, 4, "corte de giro, rad/s"),
    (0x1448, 4, "marcha engatada: 0 neutro, 1..n à frente, 10 ré"),
    (0x2CF0, 4, "quatro bytes, um por roda na ordem RL RR FL FR; tyres.md, sem nome no executável"),
    (0x2D00, 16, "float acumulado por roda, ligado ao byte em +0x2cf0"),
)


def documented_at(offset: int, size: int = 4) -> str | None:
    end = offset + size
    for start, span, note in DOCUMENTED:
        if offset < start + span and end > start:
            return note
    return None


def anchor_at(offset: int, size: int = 4, anchors: list[Anchor] | None = None) -> Anchor | None:
    end = offset + size
    for item in anchors or confirmed_anchors():
        if offset < item.offset + item.size and end > item.offset:
            return item
    return None


def stride_layouts() -> tuple[dict, ...]:
    """Estruturas repetidas já confirmadas. A comparação mede, não batiza."""
    return (
        {
            "base": WHEEL_BLOCK_BASE,
            "stride": WHEEL_STRIDE,
            "count": 4,
            "names": WHEEL_ORDER,
            "label": "bloco de geometria da roda",
        },
        {
            "base": WHEEL_HEADER_BASE,
            "stride": WHEEL_STRIDE,
            "count": 4,
            "names": WHEEL_ORDER,
            "label": "cabeçalho da roda",
        },
    )
