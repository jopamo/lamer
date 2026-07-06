#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import math
import shutil
import struct
import subprocess
import tarfile
import urllib.request
import wave
import zipfile
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, NoReturn, Optional


ROOT = Path(__file__).resolve().parent
SOURCES_ROOT = ROOT / "sources"
WAV_ROOT = ROOT / "wav"
WGET = shutil.which("wget")


@dataclass(frozen=True)
class Asset:
    filename: str
    url: str


@dataclass(frozen=True)
class Source:
    id: str
    label: str
    priority: int
    default: bool
    purpose: str
    license_note: str
    assets: tuple[Asset, ...]
    md5_manifest: Optional[str] = None


SOURCES: tuple[Source, ...] = (
    Source(
        id="ebu-sqam",
        label="EBU SQAM FLAC package",
        priority=1,
        default=True,
        purpose="Primary music/transient/tonal quality corpus.",
        license_note="Testing/evaluation only; not for commercial use.",
        assets=(
            Asset(
                filename="TECH3253_SQAM_FLAC.zip",
                url="https://qc.ebu.io/testmaterials/523/1/download/",
            ),
        ),
    ),
    Source(
        id="mit-sqam",
        label="MIT SQAM WAV selected clips",
        priority=2,
        default=True,
        purpose="Small set of revealing transient, tonal, solo, and speech clips.",
        license_note="Testing/evaluation only; not for commercial use.",
        assets=(
            Asset("gspi35_1.wav", "ftp://ftp.tnt.uni-hannover.de/pub/MPEG/audio/sqam/gspi35_1.wav"),
            Asset("harp40_1.wav", "ftp://ftp.tnt.uni-hannover.de/pub/MPEG/audio/sqam/harp40_1.wav"),
            Asset("trpt21_2.wav", "ftp://ftp.tnt.uni-hannover.de/pub/MPEG/audio/sqam/trpt21_2.wav"),
            Asset("sopr44_1.wav", "ftp://ftp.tnt.uni-hannover.de/pub/MPEG/audio/sqam/sopr44_1.wav"),
            Asset("bass47_1.wav", "ftp://ftp.tnt.uni-hannover.de/pub/MPEG/audio/sqam/bass47_1.wav"),
            Asset("quar48_1.wav", "ftp://ftp.tnt.uni-hannover.de/pub/MPEG/audio/sqam/quar48_1.wav"),
            Asset("spme50_1.wav", "ftp://ftp.tnt.uni-hannover.de/pub/MPEG/audio/sqam/spme50_1.wav"),
            Asset("spfe49_1.wav", "ftp://ftp.tnt.uni-hannover.de/pub/MPEG/audio/sqam/spfe49_1.wav"),
        ),
    ),
    Source(
        id="xiph",
        label="Xiph.org test media",
        priority=3,
        default=True,
        purpose="Real-program stereo FLAC soundtracks for compression testing.",
        license_note="Hosted as test media for compression evaluation.",
        assets=(
            Asset("ED-CM-St-16bit.flac", "https://media.xiph.org/ED/ED-CM-St-16bit.flac"),
            Asset("BigBuckBunny-stereo.flac", "https://media.xiph.org/BBB/BigBuckBunny-stereo.flac"),
            Asset("sintel-master-st.flac", "https://media.xiph.org/sintel/sintel-master-st.flac"),
            Asset("sintel_trailer-audio.flac", "https://media.xiph.org/sintel/sintel_trailer-audio.flac"),
            Asset("tearsofsteel-stereo.flac", "https://media.xiph.org/tearsofsteel/tearsofsteel-stereo.flac"),
        ),
    ),
    Source(
        id="librispeech-mini",
        label="Mini LibriSpeech",
        priority=4,
        default=True,
        purpose="Small speech regression set.",
        license_note="CC BY 4.0.",
        assets=(
            Asset("dev-clean-2.tar.gz", "https://openslr.trmal.net/resources/31/dev-clean-2.tar.gz"),
            Asset("train-clean-5.tar.gz", "https://openslr.trmal.net/resources/31/train-clean-5.tar.gz"),
            Asset("md5sum.txt", "https://openslr.trmal.net/resources/31/md5sum.txt"),
        ),
        md5_manifest="md5sum.txt",
    ),
    Source(
        id="librispeech",
        label="Full LibriSpeech dev/test subsets",
        priority=5,
        default=False,
        purpose="Larger speech-only corpus for heavier regression/perf runs.",
        license_note="CC BY 4.0.",
        assets=(
            Asset("dev-clean.tar.gz", "https://openslr.trmal.net/resources/12/dev-clean.tar.gz"),
            Asset("dev-other.tar.gz", "https://openslr.trmal.net/resources/12/dev-other.tar.gz"),
            Asset("test-clean.tar.gz", "https://openslr.trmal.net/resources/12/test-clean.tar.gz"),
            Asset("test-other.tar.gz", "https://openslr.trmal.net/resources/12/test-other.tar.gz"),
            Asset("md5sum.txt", "https://openslr.trmal.net/resources/12/md5sum.txt"),
        ),
        md5_manifest="md5sum.txt",
    ),
)

SOURCE_BY_ID = {source.id: source for source in SOURCES}
DEFAULT_SOURCE_IDS = tuple(source.id for source in SOURCES if source.default)


def log(message: str) -> None:
    print(message, flush=True)


def fail(message: str) -> NoReturn:
    raise SystemExit(message)


def ensure_layout() -> None:
    SOURCES_ROOT.mkdir(parents=True, exist_ok=True)
    WAV_ROOT.mkdir(parents=True, exist_ok=True)


def source_dir(source: Source) -> Path:
    return SOURCES_ROOT / source.id


def safe_unlink(path: Path) -> None:
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def resolve_sources(requested_ids: list[str], include_full_librispeech: bool) -> list[Source]:
    if requested_ids:
        unknown = [source_id for source_id in requested_ids if source_id not in SOURCE_BY_ID]
        if unknown:
            fail(f"unknown source id(s): {', '.join(unknown)}")
        wanted = set(requested_ids)
    else:
        wanted = set(DEFAULT_SOURCE_IDS)

    if include_full_librispeech:
        wanted.add("librispeech")

    return [source for source in SOURCES if source.id in wanted]


def archive_kind(path: Path) -> Optional[str]:
    name = path.name.lower()
    if name.endswith(".zip"):
        return "zip"
    if name.endswith(".tar.gz"):
        return "tar.gz"
    return None


def human_size(num_bytes: int) -> str:
    units = ["B", "KiB", "MiB", "GiB", "TiB"]
    size = float(num_bytes)
    for unit in units:
        if size < 1024.0 or unit == units[-1]:
            return f"{size:.1f} {unit}"
        size /= 1024.0
    return f"{num_bytes} B"


def download_file(url: str, dest: Path, force: bool) -> None:
    if dest.exists() and dest.stat().st_size > 0 and not force:
        log(f"  keep  {dest.relative_to(ROOT)}")
        return

    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_name(dest.name + ".part")
    safe_unlink(tmp)
    log(f"  fetch {url}")

    try:
        if WGET is not None:
            subprocess.run(
                [WGET, "--no-verbose", "-O", str(tmp), url],
                check=True,
            )
        else:
            if url.startswith("ftp://"):
                raise RuntimeError("wget is required for FTP-backed MIT SQAM clips")
            request = urllib.request.Request(url, headers={"User-Agent": "lame-corpus/1.0"})
            with urllib.request.urlopen(request, timeout=120) as response, tmp.open("wb") as output:
                shutil.copyfileobj(response, output, length=1024 * 1024)
        tmp.replace(dest)
    except Exception:
        safe_unlink(tmp)
        raise

    log(f"  saved {dest.relative_to(ROOT)} ({human_size(dest.stat().st_size)})")


def ensure_within(base: Path, name: str) -> Path:
    base_resolved = base.resolve()
    target = (base / name).resolve()
    try:
        target.relative_to(base_resolved)
    except ValueError as exc:
        raise RuntimeError(f"archive member escapes destination: {name}") from exc
    return target


def extract_zip(archive: Path, dest_dir: Path) -> None:
    with zipfile.ZipFile(archive) as handle:
        for member in handle.infolist():
            target = ensure_within(dest_dir, member.filename)
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with handle.open(member, "r") as src, target.open("wb") as dst:
                shutil.copyfileobj(src, dst)


def extract_tar_gz(archive: Path, dest_dir: Path) -> None:
    with tarfile.open(archive, "r:gz") as handle:
        for member in handle.getmembers():
            target = ensure_within(dest_dir, member.name)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            if member.issym() or member.islnk():
                raise RuntimeError(f"refusing to extract archive link entry: {member.name}")
            fileobj = handle.extractfile(member)
            if fileobj is None:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with fileobj, target.open("wb") as dst:
                shutil.copyfileobj(fileobj, dst)


def extract_archive(archive: Path, dest_dir: Path, force: bool) -> None:
    kind = archive_kind(archive)
    if kind is None:
        return

    marker = dest_dir / f".extract-{archive.name.replace('/', '_')}.stamp"
    if marker.exists() and not force:
        log(f"  keep  extracted {archive.relative_to(ROOT)}")
        return

    log(f"  extract {archive.relative_to(ROOT)}")
    if kind == "zip":
        extract_zip(archive, dest_dir)
    else:
        extract_tar_gz(archive, dest_dir)
    marker.write_text("ok\n", encoding="utf-8")


def parse_md5_manifest(path: Path) -> dict[str, str]:
    mapping: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        digest, filename = parts[0], parts[-1].lstrip("*")
        mapping[filename] = digest.lower()
    return mapping


def md5_file(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_source_md5(source: Source) -> None:
    if source.md5_manifest is None:
        return

    manifest_path = source_dir(source) / source.md5_manifest
    if not manifest_path.is_file():
        raise RuntimeError(f"missing md5 manifest: {manifest_path}")

    checksums = parse_md5_manifest(manifest_path)
    for asset in source.assets:
        if asset.filename == source.md5_manifest:
            continue
        target = source_dir(source) / asset.filename
        expected = checksums.get(asset.filename)
        if expected is None:
            raise RuntimeError(f"no md5 entry for {asset.filename} in {manifest_path.name}")
        actual = md5_file(target)
        if actual != expected:
            raise RuntimeError(
                f"md5 mismatch for {target}: expected {expected}, got {actual}"
            )
        log(f"  md5   {target.relative_to(ROOT)}")


def fetch_source(source: Source, force: bool) -> None:
    log(f"[{source.priority}] {source.label} ({source.id})")
    log(f"  why   {source.purpose}")
    log(f"  note  {source.license_note}")

    target_dir = source_dir(source)
    target_dir.mkdir(parents=True, exist_ok=True)

    for asset in source.assets:
        download_file(asset.url, target_dir / asset.filename, force=force)

    verify_source_md5(source)

    for asset in source.assets:
        extract_archive(target_dir / asset.filename, target_dir, force=force)


def fetch_sources(sources: Iterable[Source], force: bool) -> list[tuple[str, str]]:
    failures: list[tuple[str, str]] = []
    for source in sources:
        try:
            fetch_source(source, force=force)
        except Exception as exc:  # noqa: BLE001
            failures.append((source.id, str(exc)))
            log(f"  fail  {source.id}: {exc}")
    return failures


def run_ffmpeg(ffmpeg: str, src: Path, dest: Path) -> None:
    tmp = dest.with_name(dest.stem + ".tmp.wav")
    safe_unlink(tmp)
    try:
        subprocess.run(
            [
                ffmpeg,
                "-hide_banner",
                "-loglevel",
                "error",
                "-nostdin",
                "-y",
                "-i",
                str(src),
                "-map_metadata",
                "-1",
                "-c:a",
                "pcm_s16le",
                str(tmp),
            ],
            check=True,
        )
        tmp.replace(dest)
    except Exception:
        safe_unlink(tmp)
        raise


def stage_source_wavs(force: bool) -> int:
    count = 0
    for src in sorted(SOURCES_ROOT.rglob("*.wav")):
        rel = src.relative_to(SOURCES_ROOT)
        dest = WAV_ROOT / rel
        if dest.exists() and not force:
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
        count += 1
    return count


def convert_source_flacs(force: bool) -> int:
    flacs = sorted(SOURCES_ROOT.rglob("*.flac"))
    if not flacs:
        return 0

    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise RuntimeError("ffmpeg is required to convert FLAC sources into WAV")

    count = 0
    for src in flacs:
        rel = src.relative_to(SOURCES_ROOT)
        dest = WAV_ROOT / rel.with_suffix(".wav")
        if dest.exists() and not force:
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        run_ffmpeg(ffmpeg, src, dest)
        count += 1
    return count


def write_stereo_wav(path: Path, sample_rate: int, frames: Iterator[tuple[int, int]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(2)
        handle.setsampwidth(2)
        handle.setframerate(sample_rate)
        buffer = bytearray()
        for left, right in frames:
            buffer.extend(struct.pack("<hh", left, right))
            if len(buffer) >= 64 * 1024:
                handle.writeframesraw(buffer)
                buffer.clear()
        if buffer:
            handle.writeframesraw(buffer)


def clamp16(value: float) -> int:
    ivalue = int(round(value))
    if ivalue > 32767:
        return 32767
    if ivalue < -32768:
        return -32768
    return ivalue


def gen_silence(sample_rate: int, seconds: float) -> Iterator[tuple[int, int]]:
    total = int(sample_rate * seconds)
    for _ in range(total):
        yield 0, 0


def gen_sine(sample_rate: int, seconds: float, frequency: float) -> Iterator[tuple[int, int]]:
    total = int(sample_rate * seconds)
    amplitude = 0.7 * 32767.0
    for index in range(total):
        t = index / sample_rate
        left = amplitude * math.sin(2.0 * math.pi * frequency * t)
        right = amplitude * math.sin(2.0 * math.pi * frequency * t + math.pi / 2.0)
        yield clamp16(left), clamp16(right)


def gen_log_sweep(
    sample_rate: int,
    seconds: float,
    start_hz: float,
    end_hz: float,
) -> Iterator[tuple[int, int]]:
    total = int(sample_rate * seconds)
    ratio = end_hz / start_hz
    scale = 2.0 * math.pi * start_hz * seconds / math.log(ratio)
    amplitude = 0.8 * 32767.0
    for index in range(total):
        t = index / sample_rate
        phase = scale * (ratio ** (t / seconds) - 1.0)
        sample = clamp16(amplitude * math.sin(phase))
        yield sample, sample


def gen_impulse_train(sample_rate: int, seconds: float) -> Iterator[tuple[int, int]]:
    total = int(sample_rate * seconds)
    for index in range(total):
        if index % 1152 == 0:
            yield 32767, 0
        elif (index + 576) % 1152 == 0:
            yield 0, -32768
        else:
            yield 0, 0


def gen_clipping_mix(sample_rate: int, seconds: float) -> Iterator[tuple[int, int]]:
    total = int(sample_rate * seconds)
    for index in range(total):
        t = index / sample_rate
        raw = 1.35 * 32767.0 * (
            0.60 * math.sin(2.0 * math.pi * 997.0 * t)
            + 0.60 * math.sin(2.0 * math.pi * 12000.0 * t)
        )
        left = clamp16(raw)
        right = clamp16(-raw)
        yield left, right


def gen_odd_length(sample_rate: int, frames: int) -> Iterator[tuple[int, int]]:
    amplitude = 0.65 * 32767.0
    for index in range(frames):
        t = index / sample_rate
        env = index / max(frames - 1, 1)
        left = amplitude * env * math.sin(2.0 * math.pi * 440.0 * t)
        right = amplitude * (1.0 - env / 2.0) * math.sin(2.0 * math.pi * 880.0 * t)
        yield clamp16(left), clamp16(right)


def generate_synthetic_wavs(force: bool) -> int:
    sample_rate = 44100
    outputs = (
        ("generated/silence-2s-stereo-44k1.wav", gen_silence(sample_rate, 2.0)),
        ("generated/sine-997hz-5s-stereo-44k1.wav", gen_sine(sample_rate, 5.0, 997.0)),
        (
            "generated/sweep-20hz-20khz-10s-stereo-44k1.wav",
            gen_log_sweep(sample_rate, 10.0, 20.0, 20000.0),
        ),
        ("generated/impulse-train-2s-stereo-44k1.wav", gen_impulse_train(sample_rate, 2.0)),
        ("generated/clipping-mix-3s-stereo-44k1.wav", gen_clipping_mix(sample_rate, 3.0)),
        ("generated/odd-length-8081f-stereo-44k1.wav", gen_odd_length(sample_rate, 1152 * 7 + 17)),
    )

    count = 0
    for relative, frames in outputs:
        dest = WAV_ROOT / relative
        if dest.exists() and not force:
            continue
        write_stereo_wav(dest, sample_rate, frames)
        count += 1
    return count


def print_status() -> None:
    ensure_layout()
    log(f"sources root: {SOURCES_ROOT}")
    for source in SOURCES:
        target_dir = source_dir(source)
        if not target_dir.exists():
            log(f"  {source.id:<18} missing")
            continue
        counts = Counter()
        for path in target_dir.rglob("*"):
            if path.is_file():
                ext = "".join(path.suffixes) or "<noext>"
                counts[ext.lower()] += 1
        if counts:
            summary = ", ".join(f"{ext}:{count}" for ext, count in sorted(counts.items()))
        else:
            summary = "empty"
        log(f"  {source.id:<18} {summary}")

    total_wavs = sum(1 for _ in WAV_ROOT.rglob("*.wav"))
    generated_wavs = sum(1 for _ in (WAV_ROOT / "generated").glob("*.wav")) if (WAV_ROOT / "generated").exists() else 0
    log(f"wav root: {WAV_ROOT}")
    log(f"  total wav files: {total_wavs}")
    log(f"  generated wavs:  {generated_wavs}")


def command_fetch(args: argparse.Namespace) -> int:
    ensure_layout()
    failures = fetch_sources(
        resolve_sources(args.sources, include_full_librispeech=args.with_full_librispeech),
        force=args.force,
    )
    if failures:
        for source_id, message in failures:
            log(f"failed: {source_id}: {message}")
        return 1
    return 0


def command_prepare_wav(args: argparse.Namespace) -> int:
    ensure_layout()
    copied = stage_source_wavs(force=args.force)
    converted = convert_source_flacs(force=args.force)
    log(f"prepared wav tree: copied {copied} existing wav(s), converted {converted} flac file(s)")
    return 0


def command_generate(args: argparse.Namespace) -> int:
    ensure_layout()
    count = generate_synthetic_wavs(force=args.force)
    log(f"generated {count} synthetic wav fixture(s)")
    return 0


def command_prepare(args: argparse.Namespace) -> int:
    ensure_layout()
    failures = fetch_sources(
        resolve_sources([], include_full_librispeech=args.with_full_librispeech),
        force=args.force,
    )
    copied = stage_source_wavs(force=args.force)
    converted = convert_source_flacs(force=args.force)
    generated = generate_synthetic_wavs(force=args.force)
    log(
        "prepare complete: "
        f"copied {copied} wav(s), converted {converted} flac(s), generated {generated} synthetic wav(s)"
    )
    if failures:
        for source_id, message in failures:
            log(f"failed: {source_id}: {message}")
        return 1
    return 0


def command_status(_args: argparse.Namespace) -> int:
    print_status()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Fetch and stage a broad local sample corpus for LAME testing.",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    fetch_parser = subparsers.add_parser("fetch", help="download the configured source corpus")
    fetch_parser.add_argument("sources", nargs="*", help="optional source ids to fetch explicitly")
    fetch_parser.add_argument(
        "--with-full-librispeech",
        action="store_true",
        help="include the optional larger LibriSpeech dev/test subsets",
    )
    fetch_parser.add_argument("--force", action="store_true", help="re-download and re-extract assets")
    fetch_parser.set_defaults(func=command_fetch)

    prepare_wav_parser = subparsers.add_parser(
        "prepare-wav",
        help="copy existing WAV sources and convert all FLAC sources into samples/wav",
    )
    prepare_wav_parser.add_argument("--force", action="store_true", help="overwrite staged WAV outputs")
    prepare_wav_parser.set_defaults(func=command_prepare_wav)

    generate_parser = subparsers.add_parser("generate", help="create synthetic WAV fixtures")
    generate_parser.add_argument("--force", action="store_true", help="overwrite generated WAV fixtures")
    generate_parser.set_defaults(func=command_generate)

    prepare_parser = subparsers.add_parser(
        "prepare",
        help="fetch the default corpus, stage WAVs, and generate synthetic fixtures",
    )
    prepare_parser.add_argument(
        "--with-full-librispeech",
        action="store_true",
        help="include the optional larger LibriSpeech dev/test subsets",
    )
    prepare_parser.add_argument("--force", action="store_true", help="refresh downloads and WAV outputs")
    prepare_parser.set_defaults(func=command_prepare)

    status_parser = subparsers.add_parser("status", help="show what is present locally")
    status_parser.set_defaults(func=command_status)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
