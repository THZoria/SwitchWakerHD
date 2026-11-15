"""Private icon formatting tests, using synthetic images only (no game assets)."""
from pathlib import Path
import tempfile
import unittest

import prepare_branding

try:
    from PIL import Image
except ImportError:
    Image = None


@unittest.skipIf(Image is None, "Pillow is optional; use build/switch2-branding-venv")
class BrandingTests(unittest.TestCase):
    def test_rgb_jpeg_size_aspect_ratio_and_transparency(self):
        with tempfile.TemporaryDirectory(dir=prepare_branding.ROOT / "build", prefix="branding-test-") as directory:
            root = Path(directory)
            source = root / "synthetic.png"
            Image.new("RGBA", (1000, 200), (255, 0, 0, 255)).save(source)
            output = root / "icon.jpg"
            prepare_branding.render_icon(source, output)
            with Image.open(output) as icon:
                self.assertEqual(icon.size, (256, 256))
                self.assertEqual(icon.mode, "RGB")
                self.assertEqual(icon.format, "JPEG")
                self.assertGreater(icon.getpixel((128, 128))[0], 240)
                self.assertLess(icon.getpixel((128, 70))[0], 30)
            Image.new("RGBA", (256, 256), (255, 0, 0, 0)).save(source)
            prepare_branding.render_icon(source, output)
            with Image.open(output) as icon:
                self.assertLess(icon.getpixel((128, 128))[0], 30)

    def test_deterministic_output(self):
        with tempfile.TemporaryDirectory(dir=prepare_branding.ROOT / "build", prefix="branding-test-") as directory:
            root = Path(directory)
            source = root / "synthetic.png"
            Image.new("RGBA", (100, 300), (0, 150, 70, 255)).save(source)
            first, second = root / "a.jpg", root / "b.jpg"
            prepare_branding.render_icon(source, first)
            prepare_branding.render_icon(source, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())


if __name__ == "__main__":
    unittest.main()
