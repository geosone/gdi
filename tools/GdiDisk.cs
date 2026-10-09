// GdiDisk.cs - disk set tools of the GeosOne DOS Installer, loaded by
// GdiDisk.ps1 with Add-Type (C# 5: Windows PowerShell 5.1 and pwsh):
//   Fat12Image   1.44 MB FAT12 floppy images (write, contiguous files)
//   Fat12Reader  read the files of a FAT12/FAT16 floppy image
//   MsExpand     expand SZDD and KWAJ files (COMPRESS.EXE / EXPAND.EXE)
//   Gda          GDA archives for INSTALL.EXE (see src/archive.c)
//
// Copyright (C) 2026 GeosOne.  GNU General Public License version 3.

using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;

namespace GeosOne.Gdi
{
	public static class Crc32
	{
		static readonly uint[] table = MakeTable();

		static uint[] MakeTable()
		{
			uint[] t = new uint[256];
			for (uint n = 0; n < 256; n++) {
				uint c = n;
				for (int k = 0; k < 8; k++)
					c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
				t[n] = c;
			}
			return t;
		}

		public static uint Compute(byte[] data)
		{
			uint c = 0xFFFFFFFFu;
			foreach (byte b in data)
				c = table[(c ^ b) & 0xFF] ^ (c >> 8);
			return c ^ 0xFFFFFFFFu;
		}
	}

	static class DosTime
	{
		public static ushort Time(DateTime t) { return (ushort)(t.Hour << 11 | t.Minute << 5 | t.Second / 2); }
		public static ushort Date(DateTime t) { return (ushort)((Math.Max(t.Year, 1980) - 1980) << 9 | t.Month << 5 | t.Day); }
		public static DateTime From(ushort date, ushort time)
		{
			try {
				return new DateTime((date >> 9) + 1980, Math.Max(1, (date >> 5) & 15), Math.Max(1, date & 31),
					time >> 11, (time >> 5) & 63, (time & 31) * 2);
			} catch (ArgumentException) {
				return new DateTime(1980, 1, 1);
			}
		}
	}

	// --- boot sectors ----------------------------------------------------------

	public static class BootSectors
	{
		// own boot sector (tools/boot/bs9x.asm): boots the IO.SYS of
		// MS-DOS 7.x (Win9x) from FAT12/FAT16
		public static byte[] Win9xFat12 { get { return Convert.FromBase64String(
			"6zyQTVNXSU40LjEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAxyY7RvPx7Fge9eADFdgAeVhZVvyIFiX4AiU4CsQv886QGH70AfMZF/g+LRhiiJgWIViSKRhCY92YWA0YcE1YeA0YOEcpSUL8AB7kBAOiDAHJguCAA92YRi14LAdhIMdL381taAdiD0gCJRvyJVv6/AAe5EABRV75VfbkLAPOmX1l0B4PHIOLt6yiLfRqNRf6KTg0w7ffhA0b8E1b+V78AB7kEAOgsAF9yCIpWJOoAAnAAvPR7vmB9rAjAdAm0DrsHAM0Q6/IxwM0WXh+PBI9EAs0ZUFJRifv3dhj+wojRMdL3dhqI1ojF0MzQzAjhilYkvgMAuAECzRNzDDHAzRNOdfJZWlj5w1laWAN+C4PAAYPSAOK++MNJTyAgICAgIFNZUw0KS2VpbiBTeXN0ZW0gLyBObyBzeXN0ZW06IElPLlNZUw0KAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAVao="); } }
	}

	// --- FAT12 floppy image (1.44 MB), files in the root directory --------

	public class Fat12Image
	{
		public const int Size = 1474560;
		const int Sectors = 2880, RootEntries = 224, FatSectors = 9, DataStart = 1 + 2 * FatSectors + 14;
		const int Clusters = Sectors - DataStart;   // 2847, numbered 2..2848
		readonly byte[] img = new byte[Size];
		int nextCluster = 2, nextEntry = 0;
		string label;   // volume label entry, written last (IO.SYS must come first)

		// boot: 512-byte boot sector to take the boot code from (null: a
		// non-system disk that says so); its BPB is replaced
		public Fat12Image(byte[] boot, string label, uint serial)
		{
			if (boot != null && boot.Length >= 512) {
				Array.Copy(boot, img, 512);
				Encoding.ASCII.GetBytes("MSWIN4.1").CopyTo(img, 3);   // as Win9x FORMAT/SYS
			} else
				WriteNoSystemBoot();
			byte[] bpb = {
				0x00, 0x02,             // bytes per sector
				0x01,                   // sectors per cluster
				0x01, 0x00,             // reserved sectors
				0x02,                   // FATs
				(byte)RootEntries, 0x00,
				0x40, 0x0B,             // 2880 sectors
				0xF0,                   // media
				(byte)FatSectors, 0x00,
				0x12, 0x00,             // sectors per track
				0x02, 0x00,             // heads
				0, 0, 0, 0,             // hidden
				0, 0, 0, 0,             // large sector count
				0x00, 0x00, 0x29        // drive, reserved, extended boot signature
			};
			Array.Copy(bpb, 0, img, 11, bpb.Length);
			BitConverter.GetBytes(serial).CopyTo(img, 39);
			Encoding.ASCII.GetBytes(Pad(label ?? "NO NAME", 11)).CopyTo(img, 43);
			Encoding.ASCII.GetBytes("FAT12   ").CopyTo(img, 54);
			img[510] = 0x55;
			img[511] = 0xAA;
			for (int f = 0; f < 2; f++) {
				int o = (1 + f * FatSectors) * 512;
				img[o] = 0xF0; img[o + 1] = 0xFF; img[o + 2] = 0xFF;
			}
			this.label = label;
		}

		void WriteNoSystemBoot()
		{
			// jmp 3Eh; code at 3Eh prints the message at 60h, waits for a key, INT 19h
			byte[] code = {
				0xFA, 0x31, 0xC0, 0x8E, 0xD8, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0xFB, 0xBE, 0x60, 0x7C,
				0xAC, 0x08, 0xC0, 0x74, 0x09, 0xB4, 0x0E, 0xBB, 0x07, 0x00, 0xCD, 0x10, 0xEB, 0xF2,
				0x31, 0xC0, 0xCD, 0x16, 0xCD, 0x19
			};
			img[0] = 0xEB; img[1] = 0x3C; img[2] = 0x90;
			Encoding.ASCII.GetBytes("GEOSONE ").CopyTo(img, 3);
			Array.Copy(code, 0, img, 0x3E, code.Length);
			Encoding.ASCII.GetBytes("\r\nNot a system disk. Insert the setup disk and press a key.\r\n\0").CopyTo(img, 0x60);
		}

		static string Pad(string s, int n)
		{
			s = s.ToUpperInvariant();
			return s.Length >= n ? s.Substring(0, n) : s.PadRight(n);
		}

		public static string Name83(string name)
		{
			string n = name.ToUpperInvariant(), b = n, e = "";
			int dot = n.LastIndexOf('.');
			if (dot >= 0) { b = n.Substring(0, dot); e = n.Substring(dot + 1); }
			if (b.Length == 0 || b.Length > 8 || e.Length > 3)
				throw new ArgumentException("not an 8.3 name: " + name);
			return b.PadRight(8) + e.PadRight(3);
		}

		public long FreeBytes { get { return (long)(Clusters + 2 - nextCluster) * 512; } }
		public int FreeEntries { get { return RootEntries - nextEntry - (string.IsNullOrEmpty(label) ? 0 : 1); } }

		void SetFat(int cl, int val)
		{
			for (int f = 0; f < 2; f++) {
				int o = (1 + f * FatSectors) * 512 + cl * 3 / 2;
				if ((cl & 1) == 0) {
					img[o] = (byte)val;
					img[o + 1] = (byte)((img[o + 1] & 0xF0) | (val >> 8));
				} else {
					img[o] = (byte)((img[o] & 0x0F) | (val << 4));
					img[o + 1] = (byte)(val >> 4);
				}
			}
		}

		void AddEntry(string name11, byte attr, int cluster, long size, DateTime t)
		{
			if (nextEntry >= RootEntries)
				throw new InvalidOperationException("root directory full");
			int o = (1 + 2 * FatSectors) * 512 + nextEntry++ * 32;
			Encoding.ASCII.GetBytes(name11).CopyTo(img, o);
			img[o + 11] = attr;
			BitConverter.GetBytes(DosTime.Time(t)).CopyTo(img, o + 22);
			BitConverter.GetBytes(DosTime.Date(t)).CopyTo(img, o + 24);
			BitConverter.GetBytes((ushort)cluster).CopyTo(img, o + 26);
			BitConverter.GetBytes((uint)size).CopyTo(img, o + 28);
		}

		// attr: 1 read-only, 2 hidden, 4 system, 0x20 archive
		public void AddFile(string name, byte[] data, DateTime time, byte attr)
		{
			int n = (data.Length + 511) / 512;
			if ((long)data.Length > FreeBytes)
				throw new InvalidOperationException("disk full: " + name);
			int first = n > 0 ? nextCluster : 0;
			for (int i = 0; i < n; i++)
				SetFat(nextCluster + i, i == n - 1 ? 0xFFF : nextCluster + i + 1);
			Array.Copy(data, 0, img, (DataStart + nextCluster - 2) * 512, data.Length);
			nextCluster += n;
			AddEntry(Name83(name), attr, first, data.Length, time);
		}

		public void Save(string path)
		{
			if (!string.IsNullOrEmpty(label)) {
				AddEntry(Pad(label, 11), 0x08, 0, 0, DateTime.Now);
				label = null;
			}
			File.WriteAllBytes(path, img);
		}
	}

	// --- reading floppy images ---------------------------------------------

	public class Fat12Reader
	{
		readonly byte[] img;
		readonly int bps, spc, rsv, nfat, rootEnt, fatSec, rootStart, dataStart;
		readonly bool fat16;
		public readonly Dictionary<string, byte[]> Files = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
		public readonly Dictionary<string, DateTime> Times = new Dictionary<string, DateTime>(StringComparer.OrdinalIgnoreCase);
		public byte[] BootSector;

		public Fat12Reader(byte[] image)
		{
			img = image;
			BootSector = new byte[512];
			Array.Copy(img, BootSector, 512);
			bps = BitConverter.ToUInt16(img, 11);
			spc = img[13];
			rsv = BitConverter.ToUInt16(img, 14);
			nfat = img[16];
			rootEnt = BitConverter.ToUInt16(img, 17);
			fatSec = BitConverter.ToUInt16(img, 22);
			int total = BitConverter.ToUInt16(img, 19);
			if (total == 0) total = BitConverter.ToInt32(img, 32);
			if (bps == 0 || spc == 0)
				throw new InvalidDataException("no FAT boot sector");
			rootStart = (rsv + nfat * fatSec) * bps;
			dataStart = rootStart + rootEnt * 32;
			fat16 = (total - dataStart / bps) / spc >= 4085;
			ReadDir(rootStart, rootEnt, -1, "");
		}

		int Next(int cl)
		{
			int o = rsv * bps;
			if (fat16) return BitConverter.ToUInt16(img, o + cl * 2);
			int v = BitConverter.ToUInt16(img, o + cl * 3 / 2);
			return (cl & 1) != 0 ? v >> 4 : v & 0xFFF;
		}

		byte[] Chain(int cl, long size)
		{
			var ms = new MemoryStream();
			int csize = bps * spc, guard = 0;
			int end = fat16 ? 0xFFF8 : 0xFF8;
			while (cl >= 2 && cl < end && (size < 0 || ms.Length < size) && guard++ < 65536) {
				ms.Write(img, dataStart + (cl - 2) * csize, csize);
				cl = Next(cl);
			}
			byte[] d = ms.ToArray();
			if (size >= 0 && d.Length > size)
				Array.Resize(ref d, (int)size);
			return d;
		}

		void ReadDir(int offset, int entries, int cluster, string prefix)
		{
			byte[] dir = cluster < 0 ? null : Chain(cluster, -1);
			int n = cluster < 0 ? entries : dir.Length / 32;
			for (int i = 0; i < n; i++) {
				byte[] src = cluster < 0 ? img : dir;
				int o = cluster < 0 ? offset + i * 32 : i * 32;
				if (src[o] == 0) break;
				if (src[o] == 0xE5 || src[o + 11] == 0x0F || (src[o + 11] & 0x08) != 0) continue;
				string b = Encoding.ASCII.GetString(src, o, 8).TrimEnd(), e = Encoding.ASCII.GetString(src, o + 8, 3).TrimEnd();
				string name = prefix + b + (e.Length > 0 ? "." + e : "");
				int cl = BitConverter.ToUInt16(src, o + 26);
				if ((src[o + 11] & 0x10) != 0) {
					if (b != "." && b != "..")
						ReadDir(0, 0, cl, name + "\\");
					continue;
				}
				Files[name] = Chain(cl, BitConverter.ToUInt32(src, o + 28));
				Times[name] = DosTime.From(BitConverter.ToUInt16(src, o + 24), BitConverter.ToUInt16(src, o + 22));
			}
		}
	}

	// --- SZDD / KWAJ ---------------------------------------------------------

	public static class MsExpand
	{
		public static bool IsCompressed(byte[] d)
		{
			return d.Length >= 14 && (Magic(d, "SZDD") || Magic(d, "KWAJ"));
		}

		static bool Magic(byte[] d, string m)
		{
			return Encoding.ASCII.GetString(d, 0, 4) == m && d[4] == 0x88 && d[5] == 0xF0 && d[6] == 0x27;
		}

		// expanded data; name: the original name when the file stores it
		// (SZDD: the missing last character, KWAJ: name/extension fields)
		public static byte[] Expand(byte[] d, string packedName, out string name)
		{
			name = null;
			if (Magic(d, "SZDD")) {
				if (d[8] != 'A') throw new InvalidDataException("SZDD method " + (char)d[8]);
				if (d[9] != 0 && packedName != null && packedName.EndsWith("_"))
					name = packedName.Substring(0, packedName.Length - 1) + (char)d[9];
				return Lzss(d, 14, BitConverter.ToInt32(d, 10));
			}
			if (!Magic(d, "KWAJ"))
				throw new InvalidDataException("not SZDD/KWAJ");
			int method = BitConverter.ToUInt16(d, 8), dataOfs = BitConverter.ToUInt16(d, 10), flags = BitConverter.ToUInt16(d, 12);
			int p = 14, length = -1;
			if ((flags & 1) != 0) { length = BitConverter.ToInt32(d, p); p += 4; }
			if ((flags & 2) != 0) p += 2;
			if ((flags & 4) != 0) p += 2 + BitConverter.ToUInt16(d, p);
			string fn = null, ext = null;
			if ((flags & 8) != 0) { int s = p; while (d[p] != 0 && p - s < 8) p++; fn = Encoding.ASCII.GetString(d, s, p - s); if (d[p] == 0) p++; }
			if ((flags & 16) != 0) { int s = p; while (d[p] != 0 && p - s < 3) p++; ext = Encoding.ASCII.GetString(d, s, p - s); if (d[p] == 0) p++; }
			if (fn != null)
				name = fn + (ext != null && ext.Length > 0 ? "." + ext : "");
			switch (method) {
			case 0: {
				int n = length >= 0 ? length : d.Length - dataOfs;
				byte[] r = new byte[n];
				Array.Copy(d, dataOfs, r, 0, n);
				return r;
			}
			case 1: {
				int n = length >= 0 ? length : d.Length - dataOfs;
				byte[] r = new byte[n];
				for (int i = 0; i < n; i++) r[i] = (byte)(d[dataOfs + i] ^ 0xFF);
				return r;
			}
			case 2: return Lzss(d, dataOfs, length);
			case 3: return Lzh(d, dataOfs, length);
			default: throw new InvalidDataException("KWAJ method " + method);
			}
		}

		static byte[] Lzss(byte[] d, int p, int length)
		{
			byte[] win = new byte[4096];
			for (int i = 0; i < 4096; i++) win[i] = 0x20;
			int pos = 4096 - 16;
			var o = new MemoryStream();
			while (p < d.Length && (length < 0 || o.Length < length)) {
				int ctl = d[p++];
				for (int bit = 0; bit < 8 && p < d.Length; bit++) {
					if ((ctl & (1 << bit)) != 0) {
						byte c = d[p++];
						o.WriteByte(c);
						win[pos] = c; pos = (pos + 1) & 4095;
					} else {
						if (p + 1 >= d.Length) break;
						int m = d[p++], l = d[p++];
						int mp = m | (l & 0xF0) << 4, ml = (l & 0x0F) + 3;
						while (ml-- > 0) {
							byte c = win[mp];
							o.WriteByte(c);
							win[pos] = c; pos = (pos + 1) & 4095; mp = (mp + 1) & 4095;
						}
					}
				}
			}
			byte[] r = o.ToArray();
			if (length >= 0 && r.Length > length) Array.Resize(ref r, length);
			return r;
		}

		// KWAJ method 3: LZ77 with 5 Huffman tables (as libmspack kwajd.c)
		class Bits
		{
			readonly byte[] d; int p; uint buf; int n;
			public Bits(byte[] data, int pos) { d = data; p = pos; }
			public bool End { get { return p >= d.Length && n <= 0; } }
			public int Read(int k)
			{
				while (n < k) { buf = buf << 8 | (p < d.Length ? d[p] : (byte)0); p++; n += 8; }
				n -= k;
				return (int)((buf >> n) & ((1u << k) - 1));
			}
		}

		class Huff
		{
			readonly int[] count = new int[17];
			readonly int[] symbol;
			public Huff(byte[] lens)
			{
				symbol = new int[lens.Length];
				int[] offs = new int[18];
				foreach (byte l in lens) count[l]++;
				count[0] = 0;
				for (int l = 1; l < 17; l++) offs[l + 1] = offs[l] + count[l];
				for (int s = 0; s < lens.Length; s++)
					if (lens[s] != 0) symbol[offs[lens[s]]++] = s;
			}
			public int Decode(Bits b)
			{
				int code = 0, first = 0, index = 0;
				for (int len = 1; len < 17; len++) {
					code |= b.Read(1);
					int c = count[len];
					if (code - c < first) return symbol[index + (code - first)];
					index += c; first += c; first <<= 1; code <<= 1;
				}
				throw new InvalidDataException("KWAJ: bad Huffman code");
			}
		}

		static byte[] Lens(Bits b, int type, int n)
		{
			byte[] l = new byte[n];
			int c;
			switch (type) {
			case 0:
				c = n == 16 ? 4 : n == 32 ? 5 : n == 64 ? 6 : n == 256 ? 8 : 0;
				for (int i = 0; i < n; i++) l[i] = (byte)c;
				break;
			case 1:
				c = b.Read(4); l[0] = (byte)c;
				for (int i = 1; i < n; i++) {
					if (b.Read(1) == 0) l[i] = (byte)c;
					else if (b.Read(1) == 0) l[i] = (byte)++c;
					else { c = b.Read(4); l[i] = (byte)c; }
				}
				break;
			case 2:
				c = b.Read(4); l[0] = (byte)c;
				for (int i = 1; i < n; i++) {
					int sel = b.Read(2);
					if (sel == 3) c = b.Read(4); else c += sel - 1;
					l[i] = (byte)c;
				}
				break;
			case 3:
				for (int i = 0; i < n; i++) l[i] = (byte)b.Read(4);
				break;
			default:
				throw new InvalidDataException("KWAJ: table type " + type);
			}
			return l;
		}

		static byte[] Lzh(byte[] d, int p, int length)
		{
			var b = new Bits(d, p);
			int[] types = new int[6];
			for (int i = 0; i < 6; i++) types[i] = b.Read(4);
			Huff ml1 = new Huff(Lens(b, types[0], 16)), ml2 = new Huff(Lens(b, types[1], 16)),
				litlen = new Huff(Lens(b, types[2], 32)), offs = new Huff(Lens(b, types[3], 64)),
				lit = new Huff(Lens(b, types[4], 256));
			byte[] win = new byte[4096];
			for (int i = 0; i < 4096; i++) win[i] = 0x20;
			var o = new MemoryStream();
			int pos = 0;
			bool litRun = false;
			while ((length < 0 && !b.End) || o.Length < length) {
				int len = litRun ? ml2.Decode(b) : ml1.Decode(b);
				if (len > 0) {
					len += 2;
					litRun = false;
					int offset = offs.Decode(b) << 6 | b.Read(6);
					while (len-- > 0) {
						win[pos] = win[(pos + 4096 - offset) & 4095];
						o.WriteByte(win[pos]);
						pos = (pos + 1) & 4095;
					}
				} else {
					len = litlen.Decode(b) + 1;
					litRun = len != 32;
					while (len-- > 0) {
						win[pos] = (byte)lit.Decode(b);
						o.WriteByte(win[pos]);
						pos = (pos + 1) & 4095;
					}
				}
			}
			byte[] r = o.ToArray();
			if (length >= 0 && r.Length > length) Array.Resize(ref r, length);
			return r;
		}
	}

	// --- GDA archives --------------------------------------------------------

	public class GdaEntry
	{
		public string Name;      // name in the archive, e.g. "DOS\\EDIT.COM"
		public byte[] Data;
		public DateTime Time;
		public GdaEntry(string name, byte[] data, DateTime time) { Name = name; Data = data; Time = time; }
	}

	public static class Gda
	{
		public static byte[] Deflate(byte[] data)
		{
			var ms = new MemoryStream();
			using (var z = new DeflateStream(ms, CompressionLevel.Optimal, true))
				z.Write(data, 0, data.Length);
			return ms.ToArray();
		}

		// distribute the entries over disks; capacity(n) = bytes available
		// for the archive file on disk n (1, 2, ...); returns the disk files
		public static List<byte[]> Build(IList<GdaEntry> entries, Func<int, long> capacity, uint setId)
		{
			var disks = new List<MemoryStream>();
			long total = 0;
			foreach (var e in entries) total += e.Data.Length;
			MemoryStream cur = null;
			long cap = 0;
			Action newDisk = () => {
				cur = new MemoryStream();
				disks.Add(cur);
				cap = capacity(disks.Count);
				if (cap < 64) throw new InvalidOperationException("no space for the archive on disk " + disks.Count);
				var h = new byte[16];
				Encoding.ASCII.GetBytes("GDA1").CopyTo(h, 0);
				BitConverter.GetBytes(setId).CopyTo(h, 4);
				h[8] = (byte)disks.Count;
				BitConverter.GetBytes((uint)total).CopyTo(h, 12);
				cur.Write(h, 0, 16);
			};
			newDisk();
			foreach (var e in entries) {
				byte[] packed = Deflate(e.Data);
				byte method = 8;
				if (packed.Length >= e.Data.Length) { packed = e.Data; method = 0; }
				byte[] name = Encoding.ASCII.GetBytes(e.Name.ToUpperInvariant());
				if (name.Length > 79) throw new ArgumentException("name too long: " + e.Name);
				uint crc = Crc32.Compute(e.Data);
				int done = 0;
				bool cont = false;
				for (;;) {
					int hdr = 4 + name.Length + 16;
					long room = cap - cur.Length - hdr - 1;   // 1: end mark
					if (room < 1 || (!cont && room < 512 && packed.Length > room)) {
						cur.WriteByte((byte)'E');   // the file starts on the next disk
						newDisk();
						continue;
					}
					int part = (int)Math.Min(room, packed.Length - done);
					bool more = done + part < packed.Length;
					cur.WriteByte((byte)'F');
					cur.WriteByte((byte)((cont ? 1 : 0) | (more ? 2 : 0)));
					cur.WriteByte(method);
					cur.WriteByte((byte)name.Length);
					cur.Write(name, 0, name.Length);
					var h = new byte[16];
					BitConverter.GetBytes((uint)e.Data.Length).CopyTo(h, 0);
					BitConverter.GetBytes(crc).CopyTo(h, 4);
					BitConverter.GetBytes(DosTime.Time(e.Time)).CopyTo(h, 8);
					BitConverter.GetBytes(DosTime.Date(e.Time)).CopyTo(h, 10);
					BitConverter.GetBytes((uint)part).CopyTo(h, 12);
					cur.Write(h, 0, 16);
					cur.Write(packed, done, part);
					done += part;
					if (!more) break;
					// the file continues at the start of the next disk
					newDisk();
					cont = true;
				}
			}
			cur.WriteByte((byte)'E');
			var result = new List<byte[]>();
			foreach (var d in disks) {
				byte[] b = d.ToArray();
				b[9] = (byte)disks.Count;
				result.Add(b);
			}
			return result;
		}
	}
}
