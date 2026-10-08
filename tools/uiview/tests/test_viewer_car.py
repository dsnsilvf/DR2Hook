"""Escolhas do exportador do carro para o viewer3d (viewer_car), sem abrir o jogo."""

import unittest

from tools.uiview.car.viewer_car import keep_material, pick_texture

ASSETS = [{"n": n, "p": "img/car_fr5/" + n + ".webp"} for n in (
    "fr5_main_d.tga", "fr5_main_n.tga", "fr5_glass_d.tga", "fr5_lights_d.tga", "fr5_cabin_d.tga",
    "fr5_wheel_d_tm.tga", "fr5_wheel_d_gr.tga", "fr5_caliper_d.tga", "fr5_disc_d.tga",
)]


class KeepMaterial(unittest.TestCase):
    def test_drops_spares_blur_and_other_surfaces(self):
        self.assertFalse(keep_material("spare_wheel", "tarmac"))
        self.assertFalse(keep_material("brake_disc_blur", "tarmac"))
        self.assertFalse(keep_material("gravel_tread", "tarmac"))
        self.assertTrue(keep_material("tarmac_tread", "tarmac"))
        self.assertTrue(keep_material("gravel_tread", "gravel"))
        self.assertTrue(keep_material("body", "snow"))


class PickTexture(unittest.TestCase):
    def name(self, material, surface="tarmac"):
        hit = pick_texture(material, ASSETS, surface)
        return hit["n"] if hit else None

    def test_wheels_follow_surface(self):
        self.assertEqual(self.name("tarmac_wheel"), "fr5_wheel_d_tm.tga")
        self.assertEqual(self.name("gravel_tread", "gravel"), "fr5_wheel_d_gr.tga")

    def test_table_and_fallback(self):
        self.assertEqual(self.name("glass_exterior"), "fr5_glass_d.tga")
        self.assertEqual(self.name("lights"), "fr5_lights_d.tga")
        self.assertEqual(self.name("cabin"), "fr5_cabin_d.tga")
        self.assertEqual(self.name("caliper"), "fr5_caliper_d.tga")
        self.assertEqual(self.name("body"), "fr5_main_d.tga")
        self.assertEqual(self.name("mud_flaps"), "fr5_main_d.tga")  # sem palavra da tabela: body|paint|main


if __name__ == "__main__":
    unittest.main()
