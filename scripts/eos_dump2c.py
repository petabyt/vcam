#!/usr/bin/env python3
"""Generate vcam Canon model profiles from libgphoto2 camera dumps.

Parses the debug dumps from libgphoto2 (camlibs/ptp2/cameras/canon-eos*.txt)
which contain a camera's DeviceInfo, advertised operations and the initial
EOS GetEvent property dump, and emits a C registration file per model into
src/canon/models/.

Only device properties whose data type is hardcoded in the client's EOS event
unpacker (gphoto2 camlibs/ptp2/ptp-pack.c, "set DataType" switch) are
emitted, with the exact width the client expects. String-typed and packed
properties (Owner/Artist/..., ImageFormat*, CustomFunc*, FocusInfoEx) are
skipped - see the notes in scripts/gp_client.md.

Usage:
  scripts/eos_dump2c.py <dump.txt> <tag> [<dump.txt> <tag> ...]

Copyright Daniel C - GNU Lesser General Public License v2.1
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PTP_H = os.path.join(REPO, "src", "ptp.h")

# Client-side data types per property code (names without the
# PTP_DPC_CANON_EOS_ prefix). Values here must match the client, since it
# hardcodes them when unpacking EOS PropValueChanged events.
U32 = (
	"CameraTime UTCTime Summertime AvailableShots CaptureDestination "
	"WhiteBalanceXA WhiteBalanceXB CurrentStorage CurrentFolder ShutterCounter "
	"ModelID LensID StroboFiring StroboDispState LvCFilterKind CADarkBright "
	"ErrorForDisplay ExposureSimMode WindCut MovieRecordVolume ExtenderType "
	"AEModeMovie AFSelectFocusArea ContinousAFMode MirrorUpSetting "
	"MirrorDownStatus OLCInfoVersion PowerZoomPosition PowerZoomSpeed "
	"BuiltinStroboMode StroboETTL2Metering ColorTemperature FixedMovie "
	"AloMode LvViewTypeSelect EVFColorTemp LvAfSystem OneShotRawOn "
	"FlashChargingState MovieServoAF MultiAspect EVFOutputDevice FocusMode "
	"MirrorLockupState LensStatus TempStatus DepthOfFieldPreview EVFSharpness "
	"EVFWBMode MovieSoundRecord NetworkCommunicationMode NetworkServerRegion "
	"WftStatus CardExtension PhotoStudioMode EVFClickWBCoeffs MovSize "
	"DepthOfField Brightness GPSLogCtrl GPSDeviceActive"
).split()
U16 = (
	"AEModeDial AutoExposureMode Aperture ShutterSpeed ISOSpeed ColorSpace "
	"BatteryPower BatterySelect PTPExtensionVersion DriveMode AEB BracketMode "
	"QuickReviewTime EVFMode EVFRecordStatus HighISONoiseReduction DPOFVersion"
).split()
U8 = ("PictureStyle WhiteBalance MeteringMode ExpCompensation").split()
I32 = ("AutoPowerOff WhiteBalanceAdjustA WhiteBalanceAdjustB").split()

TYPES = {
	"u32": U32,
	"u16": U16,
	"u8": U8,
	"i32": I32,
}

# Stripped lines: optional "<seconds> " timestamp and "<file>(<n>): " source
PREFIX_RE = re.compile(r"^\s*(?:[\d.]+ )?(?:[\w/.]+\(\d+\): )?")


def load_ptp_h_codes():
	codes = {}
	with open(PTP_H) as f:
		for line in f:
			m = re.match(r"#define PTP_DPC_EOS_(\w+)\s+0x([0-9a-fA-F]+)", line)
			if m:
				codes[m.group(1)] = int(m.group(2), 16)
	return codes


def code_to_name(codes, code):
	for name, c in codes.items():
		if c == code:
			return name
	return None


def parse_dump(path):
	meta = {"model": None, "version": "", "serial": "", "vendor_ext": None}
	ops = []
	props = {}  # code -> value (first seen wins)

	lines = [PREFIX_RE.sub("", line.rstrip("\n")) for line in open(path)]

	section = None
	cur_prop = None  # (code, datasize, value or None, pending bytes/u32 list)
	for line in lines:
		stripped = line.strip()

		if stripped.startswith("Device info:"):
			section = "deviceinfo"
			continue
		if stripped.startswith("Supported operations:"):
			section = "ops"
			continue
		if re.match(r"^(Device properties|Supported properties|Device "
					r"Property Summary|Camera storage|other trace|-----)", stripped):
			section = None
			cur_prop = None
			continue

		if section == "deviceinfo":
			m = re.match(r"Model:\s+(.*)$", stripped)
			if m and meta["model"] is None:
				meta["model"] = m.group(1).strip()
			m = re.match(r"device version:\s+(.*)$", stripped)
			if m and not meta["version"]:
				meta["version"] = m.group(1).strip()
			m = re.match(r"serial number:\s+'?(.*?)'?$", stripped)
			if m and not meta["serial"]:
				meta["serial"] = m.group(1).strip()
			m = re.match(r"Vendor extension ID:\s+0x([0-9a-fA-F]+)", stripped)
			if m and meta["vendor_ext"] is None:
				meta["vendor_ext"] = int(m.group(1), 16)

		elif section == "ops":
			m = re.match(r"^0x([0-9a-fA-F]{4})(?:\s*\(|$)", stripped)
			if m:
				code = int(m.group(1), 16)
				if code not in ops:
					ops.append(code)
				continue
			if not stripped:
				continue
			section = None  # any other line ends the op list

		elif re.match(r"^event \d+: EOS prop ([0-9a-f]{3,4}) info record, "
					  r"datasize (?:is )?(\d+)$", stripped):
			m = re.match(r"^event \d+: EOS prop ([0-9a-f]{3,4}) info record, "
						 r"datasize (?:is )?(\d+)$", stripped)
			cur_prop = {"code": int(m.group(1), 16), "datasize": int(m.group(2)),
						"value": None, "bytes": []}
			props.setdefault(cur_prop["code"], cur_prop)

		elif cur_prop is not None:
			m = re.match(r"^event \d+: currentvalue of [0-9a-f]{3,4} is "
						 r"([0-9a-fA-F]+)$", stripped)
			if m:
				if cur_prop["value"] is None:
					cur_prop["value"] = int(m.group(1), 16)
				cur_prop = None
				continue
			m = re.match(r"^Unknown EOS property", stripped)
			if m:
				continue
			m = re.match(r"^(\d+): 0x\s*([0-9a-fA-F]+)$", stripped)
			if m:  # uint32 dump style
				if cur_prop["value"] is None:
					cur_prop["value"] = int(m.group(2), 16)
				continue
			m = re.match(r"^(\d+): ([0-9a-fA-F]{1,2})$", stripped)
			if m:  # raw byte dump style, little endian order
				cur_prop["bytes"].append(int(m.group(2), 16))
				continue
			if re.match(r"^event \d+:", stripped):
				cur_prop = None

	# Assemble byte-dump values
	for p in props.values():
		if p["value"] is None and p["bytes"]:
			v = 0
			for i, b in enumerate(p["bytes"][:4]):
				v |= b << (8 * i)
			p["value"] = v

	return meta, ops, props


def c_string(s):
	return s.replace("\\", "\\\\").replace('"', '\\"')


def emit(tag, dumpname, meta, ops, props, codes, out):
	typed = []  # (code, ctype, value)
	for code, p in sorted(props.items()):
		name = code_to_name(codes, code)
		if name is None:
			continue
		for ctype, names in TYPES.items():
			if name in names:
				typed.append((code, ctype, p["value"] if p["value"] is not None else 0))
				break

	with open(out, "w") as f:
		f.write("// Generated by scripts/eos_dump2c.py from libgphoto2\n")
		f.write("// camlibs/ptp2/cameras/%s - do not edit by hand\n" % dumpname)
		f.write("// Model: %s, version %s\n" % (meta["model"], meta["version"]))
		f.write("#include <vcam.h>\n")
		f.write("#include <canon.h>\n\n")

		f.write("// Supported operations advertised by the camera\n")
		f.write("const uint16_t canon_model_%s_ops[] = {" % tag)
		for i, op in enumerate(ops):
			f.write("%s0x%04X," % ("\n\t" if i % 8 == 0 else " ", op))
		f.write("\n};\n\n")

		f.write("int canon_register_model_%s(vcam *cam) {\n" % tag)
		for code, ctype, value in typed:
			if ctype == "i32":
				f.write("\tcanon_add_prop_i32(cam, %s, %d);\n" % (dpc_name(codes, code), value))
			elif ctype == "u32":
				f.write("\tcanon_add_prop_u32(cam, %s, 0x%X);\n" % (dpc_name(codes, code), value & 0xFFFFFFFF))
			elif ctype == "u16":
				f.write("\tcanon_add_prop_u16(cam, %s, 0x%X);\n" % (dpc_name(codes, code), value & 0xFFFF))
			elif ctype == "u8":
				f.write("\tcanon_add_prop_u8(cam, %s, 0x%X);\n" % (dpc_name(codes, code), value & 0xFF))
		f.write("\tcanon_register_eos_ops(cam, canon_model_%s_ops,\n" % tag)
		f.write("\t\t(int)(sizeof(canon_model_%s_ops) / sizeof(canon_model_%s_ops[0])));\n" % (tag, tag))
		f.write("\treturn 0;\n")
		f.write("}\n")

	print("%s: %d ops, %d typed props, model '%s' v%s" %
		  (tag, len(ops), len(typed), meta["model"], meta["version"]))


def dpc_name(codes, code):
	# Prefer the PTP_DPC_EOS_ name, fall back to a hex literal
	for name, c in codes.items():
		if c == code:
			return "PTP_DPC_EOS_" + name
	return "0x%04X" % code


def main():
	args = sys.argv[1:]
	if not args or len(args) % 2 != 0:
		print(__doc__)
		return 1

	codes = load_ptp_h_codes()
	outdir = os.path.join(REPO, "src", "canon", "models")
	os.makedirs(outdir, exist_ok=True)

	for i in range(0, len(args), 2):
		dump, tag = args[i], args[i + 1]
		if not re.match(r"^\w+$", tag):
			print("bad tag %r" % tag)
			return 1
		meta, ops, props = parse_dump(dump)
		if not meta["model"]:
			print("%s: no model found in dump" % dump)
			return 1
		out = os.path.join(outdir, "%s.c" % tag)
		emit(tag, os.path.basename(dump), meta, ops, props, codes, out)
	return 0


if __name__ == "__main__":
	sys.exit(main())