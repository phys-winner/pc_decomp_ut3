"""Every game locale ships the same settings with translated labels and choices."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
OPTIONS = ROOT / 'android/uscript/AndroidPort'


def sections(path):
    groups = {}
    current = None
    for line in path.read_text(encoding='utf-8').splitlines():
        if line.startswith('['):
            current = line
            groups[current] = []
        elif line.strip():
            groups[current].append(line)
    return groups


class LocalizedOptionsTest(unittest.TestCase):
    def test_all_game_languages_have_complete_option_definitions(self):
        base = sections(OPTIONS / 'AndroidPortOptions.ini')
        self.assertEqual(len(base), 25)
        for language in ('RUS', 'DEU', 'FRA', 'ITA', 'ESN', 'CZE', 'HUN', 'POL'):
            translated = sections(OPTIONS / ('AndroidPortOptions-' + language + '.ini'))
            self.assertEqual(base.keys(), translated.keys(), language)
            for section, values in translated.items():
                self.assertNotIn('\ufffd', '\n'.join(values))
                if section == '[AndroidPort.AndroidPortDataStore]':
                    for tag, count in (('language', 9), ('touchGyro', 5), ('touchVehicleProfile', 5)):
                        row = next(v for v in values if v.startswith('.Choices=(Tag="' + tag + '"'))
                        self.assertEqual(len(re.findall(r'"([^"]*)"', row)) - 1, count)
                    for key in ('PhysXCaption=', 'PhysXDescription=', 'OpenALCaption=', 'OpenALDescription=', 'MaximumGraphicsCaption='):
                        self.assertTrue(any(v.startswith(key) and len(v)>len(key) for v in values), (language, key))
                    continue
                for key in ('FriendlyName=', 'Description='):
                    label = next(v for v in values if v.startswith(key))
                    self.assertGreater(len(label), len(key))
                    if (language, section, key) != ('FRA', '[AndroidPort_touchGyro UTUIDataProvider_MenuOption]', 'FriendlyName=') and not (
                        key == 'FriendlyName=' and section in ('[AndroidPort_fsr1 UTUIDataProvider_MenuOption]', '[HardwareOpenAL UTUIDataProvider_MenuOption]')):
                        self.assertNotEqual(label, next(v for v in base[section] if v.startswith(key)), (language, section))
                metadata = lambda rows: [v for v in rows if not v.startswith(('FriendlyName=', 'Description='))]
                self.assertEqual(metadata(values), metadata(base[section]), (language, section))


if __name__ == '__main__':
    unittest.main()
