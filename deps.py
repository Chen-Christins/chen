#!/usr/bin/env python3
"""Chen framework dependency manager.

Producer workflow (run on your local Linux):

    ./deps.py build      # download 3rdparty sources, compile static libs into deps/
    ./deps.py publish    # upload deps-<arch>.zip to the source release tag
    ./deps.py release    # build + publish

Consumer workflow (CI / other machines):

    ./deps.py sync       # download prebuilt deps-<arch>.zip and extract into deps/

`cmake/dependencies.cmake` consumes `deps/include` + `deps/lib`, so a synced
prebuilt bundle is enough to build the framework without recompiling any
third-party source.
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MANIFEST = ROOT / "dependencies.json"
LOCAL_OVERRIDE = ROOT / "dependencies.local.json"
CACHE_DIR = ROOT / ".deps-cache"


class DepsError(Exception):
    pass


class UI:
    RESET = "\033[0m"
    RED = "\033[31m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    BLUE = "\033[34m"
    MAGENTA = "\033[35m"
    CYAN = "\033[36m"

    def __init__(self, color=True):
        self.color = bool(color) and sys.stdout.isatty()

    def _paint(self, code, text):
        if not self.color:
            return text
        return f"{code}{text}{self.RESET}"

    def info(self, msg):
        print(f"{self._paint(self.BLUE, 'ℹ')} {msg}")

    def step(self, icon, msg):
        print(f"{icon} {self._paint(self.CYAN, msg)}")

    def ok(self, msg):
        print(f"{self._paint(self.GREEN, '✔')} {msg}")

    def warn(self, msg):
        print(f"{self._paint(self.YELLOW, '⚠')} {msg}")

    def error(self, msg):
        print(f"{self._paint(self.RED, '✖')} {msg}", file=sys.stderr)

    def path(self, text):
        return self._paint(self.MAGENTA, text)

    def progress(self, done, total):
        if not self.color:
            return
        if total:
            pct = done * 100 // total
            length = 24
            filled = length * pct // 100
            bar = "█" * filled + "░" * (length - filled)
            line = f"   {bar} {pct:3d}%  {human(done)}/{human(total)}"
        else:
            line = f"   {human(done)}"
        sys.stdout.write("\r" + line + "\x1b[K")
        sys.stdout.flush()

    def progress_end(self):
        if self.color:
            sys.stdout.write("\n")
            sys.stdout.flush()


def human(n):
    value = float(n)
    for unit in ("B", "KB", "MB", "GB"):
        if value < 1024 or unit == "GB":
            return f"{value:.1f}{unit}"
        value /= 1024


def host_arch():
    machine = platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        return "arm64"
    if machine in ("x86_64", "amd64"):
        return "amd64"
    raise DepsError(f"无法识别的架构: {machine}")


def load_json(path):
    if not path.exists():
        return {}
    try:
        with path.open(encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        raise DepsError(f"无法解析 {path.name}: {exc}") from exc


def resolve_config():
    cfg = dict(load_json(MANIFEST))
    if not cfg:
        raise DepsError(f"{MANIFEST.name} 不存在或为空")
    override = load_json(LOCAL_OVERRIDE)
    for key in ("deps-source", "deps-package", "packages", "arch", "install_prefix", "extract_dir"):
        if key in override:
            cfg[key] = override[key]
    return cfg


def resolve_arch(args, cfg):
    arch = getattr(args, "arch", None) or cfg.get("arch") or "auto"
    if arch == "auto":
        arch = host_arch()
    if arch not in ("amd64", "arm64"):
        raise DepsError(f"不支持的架构: {arch}")
    return arch


def install_prefix(cfg):
    return ROOT / cfg.get("install_prefix", "deps")


def source_dir(cfg):
    return ROOT / cfg.get("extract_dir", "3rdparty")


def package_asset(cfg, arch):
    return cfg["deps-package"]["asset"].format(arch=arch)


def source_asset(cfg):
    return cfg["deps-source"]["asset"]


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url, dest, ui, token=None):
    headers = {"User-Agent": "chen-deps"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    try:
        with urllib.request.urlopen(req, timeout=60) as resp, tmp.open("wb") as fh:
            total = int(resp.headers.get("Content-Length") or 0)
            done = 0
            while True:
                chunk = resp.read(65536)
                if not chunk:
                    break
                fh.write(chunk)
                done += len(chunk)
                ui.progress(done, total)
        ui.progress_end()
        tmp.replace(dest)
    except urllib.error.HTTPError as exc:
        tmp.unlink(missing_ok=True)
        raise DepsError(f"下载失败 HTTP {exc.code}: {url}") from exc
    except Exception as exc:
        tmp.unlink(missing_ok=True)
        raise DepsError(f"下载失败: {exc}") from exc


def github_token():
    return os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")


def fetch_digest(repo, tag, asset, ui):
    url = f"https://api.github.com/repos/{repo}/releases/tags/{tag}"
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "chen-deps"}
    token = github_token()
    if token:
        headers["Authorization"] = f"Bearer {token}"
    try:
        req = urllib.request.Request(url, headers=headers)
        with urllib.request.urlopen(req, timeout=20) as resp:
            data = json.load(resp)
    except urllib.error.HTTPError as exc:
        ui.warn(f"GitHub API 请求失败 ({exc.code})，跳过 sha256 校验")
        return None
    except Exception as exc:
        ui.warn(f"GitHub API 不可用（{exc.__class__.__name__}），跳过 sha256 校验")
        return None
    for item in data.get("assets", []):
        if item.get("name") == asset:
            digest = item.get("digest") or ""
            if digest.startswith("sha256:"):
                return digest.split(":", 1)[1]
    ui.warn(f"release {tag} 未提供 {asset} 的 sha256，跳过校验")
    return None


def verify(path, repo, tag, asset, ui, local=False):
    if local:
        ui.warn("🪄 使用本地包，跳过 sha256 校验")
        return
    ui.step("🔐", f"校验 sha256 {asset}")
    expected = fetch_digest(repo, tag, asset, ui)
    if not expected:
        return
    actual = sha256_file(path)
    if actual != expected:
        raise DepsError(f"sha256 校验失败\n  期望: {expected}\n  实际: {actual}")
    ui.ok("sha256 校验通过")


def ensure_download(repo, tag, asset, ui):
    cache = CACHE_DIR / asset
    if cache.is_file() and cache.stat().st_size > 0:
        ui.ok(f"📦 命中缓存 {ui.path(cache.name)}")
        return cache
    url = f"https://github.com/{repo}/releases/download/{tag}/{asset}"
    ui.step("📦", f"下载 {asset}")
    ui.info(url)
    download(url, cache, ui, token=github_token())
    return cache


def run(cmd, cwd, ui):
    ui.info("$ " + " ".join(str(c) for c in cmd))
    result = subprocess.run([str(c) for c in cmd], cwd=str(cwd))
    if result.returncode != 0:
        raise DepsError(f"命令失败(exit {result.returncode}): {' '.join(str(c) for c in cmd)}")


def unpack_nested(pkg, src_root, ui):
    name = pkg.get("from")
    if not name:
        return
    path = src_root / name
    if not path.is_file():
        ui.warn(f"未找到内层源码包 {name}，跳过（假设已展开）")
        return
    ui.step("📂", f"展开 {name}")
    if name.endswith(".zip"):
        with zipfile.ZipFile(path) as zf:
            zf.extractall(src_root)
    else:
        with tarfile.open(path) as tf:
            tf.extractall(src_root)


def clean_macos_cruft(roots):
    for root in roots:
        root = Path(root)
        if not root.is_dir():
            continue
        for path in list(root.rglob("__MACOSX")):
            if path.is_dir():
                shutil.rmtree(path, ignore_errors=True)
        for path in list(root.rglob(".DS_Store")):
            path.unlink(missing_ok=True)


def build_package(pkg, src_root, prefix, ui):
    name = pkg["name"]
    pkg_dir = src_root / pkg["dir"]
    if not pkg_dir.is_dir():
        raise DepsError(f"未找到源码目录: {pkg_dir}")
    kind = pkg.get("type", "cmake")
    args = pkg.get("args", [])
    jobs = str(max(1, (os.cpu_count() or 2)))
    ui.step("🔨", f"编译 {name} ({kind}, 静态 → {ui.path('deps/')})")
    prefix.mkdir(parents=True, exist_ok=True)

    if kind == "cmake":
        build_dir = pkg_dir / "build"
        if build_dir.exists():
            shutil.rmtree(build_dir)
        build_dir.mkdir(parents=True)
        run(["cmake", f"-DCMAKE_INSTALL_PREFIX={prefix}", *args, ".."], build_dir, ui)
        run(["cmake", "--build", ".", "-j", jobs], build_dir, ui)
        run(["cmake", "--install", "."], build_dir, ui)
    elif kind == "autotools":
        run(["./configure", f"--prefix={prefix}", *args], pkg_dir, ui)
        run(["make", "-j", jobs], pkg_dir, ui)
        run(["make", "install"], pkg_dir, ui)
    elif kind == "make":
        var = pkg.get("install_prefix_var", "PREFIX")
        run(["make", "-j", jobs], pkg_dir, ui)
        run(["make", "install", f"{var}={prefix}"], pkg_dir, ui)
    else:
        raise DepsError(f"未知的编译类型: {kind}")
    ui.ok(f"{name} 完成")


def valid_deps(prefix):
    prefix = Path(prefix)
    lib = prefix / "lib"
    if not (prefix / "include").is_dir() or not lib.is_dir():
        return False
    return any(lib.glob("*.a")) or any(lib.glob("*.so"))


def package_deps(cfg, arch, ui):
    prefix = install_prefix(cfg)
    bin_dir = prefix / "bin"
    out = ROOT / package_asset(cfg, arch)
    if out.exists():
        out.unlink()
    ui.step("📦", f"打包 {out.name}")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(prefix.rglob("*")):
            if not path.is_file():
                continue
            arcname = path.relative_to(ROOT)
            mode = path.stat().st_mode & 0o777
            if path.is_relative_to(bin_dir):
                mode |= 0o755
            if not (mode & 0o444):
                mode |= 0o644
            info = zipfile.ZipInfo.from_file(path, str(arcname))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (mode & 0xFFFF) << 16
            with path.open("rb") as src, zf.open(info, "w") as dst:
                shutil.copyfileobj(src, dst)
    ui.ok(f"已生成 {ui.path(out.name)}")
    return out


def extract_zip_restore(zf, dest):
    for info in zf.infolist():
        target = zf.extract(info, dest)
        mode = (info.external_attr >> 16) & 0o777
        if mode:
            try:
                os.chmod(target, mode)
            except OSError:
                pass


def ensure_bin_executable(prefix):
    bin_dir = Path(prefix) / "bin"
    if not bin_dir.is_dir():
        return
    for path in bin_dir.rglob("*"):
        if path.is_file():
            mode = path.stat().st_mode & 0o777
            path.chmod(mode | 0o755)


def install_package(zip_path, ui):
    dest = ROOT / "deps"
    tmp = Path(tempfile.mkdtemp(prefix=".deps-tmp-", dir=ROOT))
    try:
        with zipfile.ZipFile(zip_path) as zf:
            extract_zip_restore(zf, tmp)
        src = tmp / "deps"
        if not src.is_dir():
            raise DepsError(f"{Path(zip_path).name} 内未找到顶层 deps/ 目录")
        if dest.exists():
            shutil.rmtree(dest)
        shutil.move(str(src), str(dest))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if not valid_deps(dest):
        raise DepsError("解压后校验失败：deps/ 缺少 include 或 lib")
    ensure_bin_executable(dest)


def gc(cfg, ui, arch=None, keep_old=False):
    removed = []
    if keep_old:
        return removed
    keep_pkg = package_asset(cfg, arch) if arch else None
    for item in sorted(ROOT.glob("deps-*.zip")):
        if keep_pkg and item.name == keep_pkg:
            continue
        item.unlink()
        removed.append(item.name)
    for item in sorted(ROOT.glob("3rdparty.zip")):
        item.unlink()
        removed.append(item.name)
    if CACHE_DIR.is_dir():
        shutil.rmtree(CACHE_DIR)
        removed.append(".deps-cache/")
    return removed


def report_gc(removed, ui):
    if removed:
        ui.step("🧹", f"清理旧文件 {', '.join(removed)}")


def cmd_build(args, ui):
    cfg = resolve_config()
    arch = resolve_arch(args, cfg)
    src = cfg["deps-source"]
    src_root = source_dir(cfg)

    zip_path = ensure_download(src["repo"], src["tag"], source_asset(cfg), ui)
    verify(zip_path, src["repo"], src["tag"], source_asset(cfg), ui)

    if src_root.exists():
        ui.step("🧹", f"清理旧 {src_root.name}/")
        shutil.rmtree(src_root)
    ui.step("📂", f"解压源码到 {ui.path(src_root.name + '/')}")
    with zipfile.ZipFile(zip_path) as zf:
        zf.extractall(ROOT)
    if not src_root.is_dir():
        raise DepsError(f"解压后未找到 {src_root.name}/ 目录")

    for pkg in cfg.get("packages", []):
        unpack_nested(pkg, src_root, ui)

    clean_macos_cruft([ROOT, src_root])

    if args.no_build:
        ui.ok(f"已准备好源码（跳过编译）：{ui.path(src_root.name + '/')}")
        return 0

    prefix = install_prefix(cfg)
    for pkg in cfg.get("packages", []):
        build_package(pkg, src_root, prefix, ui)

    package_deps(cfg, arch, ui)
    report_gc(gc(cfg, ui, arch=arch, keep_old=args.keep_old), ui)
    ui.ok(f"🔄 生产完成：{ui.path('deps/')} + {ui.path(package_asset(cfg, arch))} (arch={arch})")
    return 0


def cmd_package(args, ui):
    cfg = resolve_config()
    arch = resolve_arch(args, cfg)
    prefix = install_prefix(cfg)
    if not valid_deps(prefix):
        raise DepsError("deps/ 不完整，先执行 ./deps.py build")
    out = package_deps(cfg, arch, ui)
    report_gc(gc(cfg, ui, arch=arch, keep_old=args.keep_old), ui)
    ui.ok(f"已打包 {ui.path(out.name)} (arch={arch})")
    return 0


def cmd_publish(args, ui):
    cfg = resolve_config()
    arch = resolve_arch(args, cfg)
    pkg = ROOT / (args.file or package_asset(cfg, arch))
    if not pkg.is_file():
        raise DepsError(f"未找到待上传的包: {pkg.name}（先执行 ./deps.py build）")
    if not shutil.which("gh"):
        raise DepsError("未找到 gh CLI，请安装 GitHub CLI 后重试")
    tag = cfg["deps-package"]["tag"]
    repo = cfg["deps-package"].get("repo", cfg["deps-source"]["repo"])
    ui.step("🚀", f"上传 {pkg.name} → {repo}@{tag}")
    run(["gh", "release", "upload", tag, pkg, "--clobber", "--repo", repo], ROOT, ui)
    ui.ok(f"已上传 {ui.path(pkg.name)} 到 {tag}")
    return 0


def cmd_release(args, ui):
    rc = cmd_build(args, ui)
    if rc != 0:
        return rc
    return cmd_publish(args, ui)


def cmd_sync(args, ui):
    cfg = resolve_config()
    arch = resolve_arch(args, cfg)
    asset = package_asset(cfg, arch)
    pkg_cfg = cfg["deps-package"]

    if args.archive:
        zip_path = Path(args.archive).expanduser()
        if not zip_path.is_file():
            raise DepsError(f"本地包不存在: {zip_path}")
        ui.step("🪄", f"使用本地包 {zip_path.name}")
        verify(zip_path, pkg_cfg["repo"], pkg_cfg["tag"], asset, ui, local=True)
    else:
        zip_path = ensure_download(pkg_cfg["repo"], pkg_cfg["tag"], asset, ui)
        verify(zip_path, pkg_cfg["repo"], pkg_cfg["tag"], asset, ui)

    install_package(zip_path, ui)
    if not args.keep_old:
        for pattern in ("3rdparty.zip",):
            for item in sorted(ROOT.glob(pattern)):
                item.unlink()
    ui.ok(f"✅ 已同步预编译依赖到 {ui.path('deps/')} (arch={arch})，无需编译")
    return 0


def cmd_current(args, ui):
    cfg = resolve_config()
    arch = resolve_arch(args, cfg)
    prefix = install_prefix(cfg)
    src_root = source_dir(cfg)
    mode = "local override" if LOCAL_OVERRIDE.exists() else "dependencies.json"
    ui.info(f"配置: source={source_asset(cfg)}@{cfg['deps-source']['tag']}, "
            f"package={package_asset(cfg, arch)}@{cfg['deps-package']['tag']} "
            f"(arch={arch}, {mode})")
    if valid_deps(prefix):
        libs = sorted(p.name for p in (prefix / "lib").glob("*.a"))
        ui.ok(f"deps/ 就绪（{len(libs)} 个静态库）: {', '.join(libs) if libs else '-'}")
    else:
        ui.warn("deps/ 未就绪（运行 ./deps.py sync 或 ./deps.py build）")
    if src_root.is_dir():
        ui.info(f"源码目录存在: {ui.path(src_root.name + '/')}")
    return 0


def cmd_list(args, ui):
    cfg = resolve_config()
    repo = cfg["deps-source"]["repo"]
    url = f"https://api.github.com/repos/{repo}/releases"
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "chen-deps"}
    token = github_token()
    if token:
        headers["Authorization"] = f"Bearer {token}"
    ui.step("🌐", f"查询 {repo} 的 Release")
    try:
        req = urllib.request.Request(url, headers=headers)
        with urllib.request.urlopen(req, timeout=20) as resp:
            releases = json.load(resp)
    except Exception as exc:
        raise DepsError(f"查询 Release 失败: {exc}") from exc
    for release in releases:
        tag = release.get("tag_name", "?")
        assets = [a.get("name") for a in release.get("assets", [])]
        print(f"🏷️  {ui.path(tag)}")
        for name in assets:
            print(f"     {name}")
    return 0


def cmd_clean(args, ui):
    removed = []
    for path in (ROOT / "deps", source_dir(resolve_config()), ROOT / "__MACOSX"):
        if path.exists():
            shutil.rmtree(path)
            removed.append(path.name + "/")
    for item in sorted(ROOT.glob("deps-*.zip")) + sorted(ROOT.glob("3rdparty.zip")):
        item.unlink()
        removed.append(item.name)
    if CACHE_DIR.is_dir():
        shutil.rmtree(CACHE_DIR)
        removed.append(".deps-cache/")
    if LOCAL_OVERRIDE.exists():
        LOCAL_OVERRIDE.unlink()
        removed.append(LOCAL_OVERRIDE.name)
    if not removed:
        ui.info("没有需要清理的内容")
        return 0
    ui.ok(f"🧹 已清理: {', '.join(removed)}")
    return 0


def build_parser():
    parser = argparse.ArgumentParser(
        prog="deps.py",
        description="chen 框架依赖管理（本地生产 deps 预编译包 / CI 消费解压）",
    )
    parser.add_argument("--no-color", action="store_true", help="禁用彩色输出")
    parser.add_argument("--keep-old", action="store_true", help="保留旧文件，不清理")
    sub = parser.add_subparsers(dest="command")

    p_build = sub.add_parser("build", help="下载源码并编译静态库到 deps/，打包 deps-<arch>.zip")
    p_build.add_argument("--arch", help="目标架构 amd64 / arm64（默认 auto）")
    p_build.add_argument("--no-build", action="store_true", help="只下载解压源码，不编译")
    p_build.set_defaults(func=cmd_build)

    p_pkg = sub.add_parser("package", help="用现有 deps/ 重新打包 deps-<arch>.zip（不编译）")
    p_pkg.add_argument("--arch", help="目标架构")
    p_pkg.set_defaults(func=cmd_package)

    p_pub = sub.add_parser("publish", help="上传 deps-<arch>.zip 到 release")
    p_pub.add_argument("--arch", help="目标架构")
    p_pub.add_argument("--file", help="指定要上传的包文件名")
    p_pub.set_defaults(func=cmd_publish)

    p_rel = sub.add_parser("release", help="build + publish")
    p_rel.add_argument("--arch", help="目标架构")
    p_rel.add_argument("--no-build", action="store_true", help="只下载解压源码，不编译")
    p_rel.add_argument("--file", help="指定要上传的包文件名")
    p_rel.set_defaults(func=cmd_release)

    p_sync = sub.add_parser("sync", help="下载并解压预编译 deps 包到 deps/（不编译）")
    p_sync.add_argument("--arch", help="目标架构")
    p_sync.add_argument("--archive", help="使用本地 deps-<arch>.zip")
    p_sync.set_defaults(func=cmd_sync)

    p_cur = sub.add_parser("current", help="显示当前依赖状态")
    p_cur.set_defaults(func=cmd_current)

    p_ls = sub.add_parser("list", aliases=["ls"], help="列出远端 release 与资产")
    p_ls.set_defaults(func=cmd_list)

    p_clean = sub.add_parser("clean", help="删除 deps/、3rdparty/、缓存与产物")
    p_clean.set_defaults(func=cmd_clean)

    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    ui = UI(color=not args.no_color and not os.environ.get("NO_COLOR"))
    if not getattr(args, "command", None):
        parser.print_help()
        return 0
    try:
        return args.func(args, ui)
    except DepsError as exc:
        ui.error(str(exc))
        return 1
    except KeyboardInterrupt:
        ui.error("已取消")
        return 130


if __name__ == "__main__":
    sys.exit(main())
