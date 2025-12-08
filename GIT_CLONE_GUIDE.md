# CSFS Git Clone - Quick Reference

## Quick Start

```bash
# Create filesystem
./csfs myfs.img create

# Enter shell and clone
./csfs myfs.img
fssh:/> git clone https://github.com/user/repo.git myrepo
fssh:/> ls myrepo
fssh:/> cat /myrepo/README
```

## What Gets Downloaded?

When you clone a repository, CSFS downloads these files (if they exist):
- README.md / README - Documentation
- LICENSE / COPYING - License information  
- Makefile / CMakeLists.txt - Build configuration
- setup.py - Python setup
- .gitignore - Git ignore patterns

## Examples

### Clone Octocat Hello-World
```bash
git clone https://github.com/octocat/Hello-World.git
# Downloads ~8 files, 100 bytes total
```

### Clone Linux Kernel
```bash
git clone https://github.com/torvalds/linux.git linux
# Downloads ~8 files, ~80KB total (Makefile is large!)
```

### Clone from Different Branch
```bash
# The system automatically detects the default branch
# (main, master, or any custom default)
git clone https://github.com/python/cpython.git
```

### List Cloned Repository
```bash
fssh:/> ls myrepo
.git        [DIR]
README      13 B
Makefile    1024 B
[... other files ...]
```

### Read Files from Clone
```bash
fssh:/> cat /myrepo/README
fssh:/> cat /myrepo/LICENSE
```

## Limitations & Notes

⚠️ **Not a Full Clone**: Only downloads key files, not entire repository

✅ **Real Content**: Files are actual GitHub content, not dummy data

✅ **All GitHub Repos**: Works with any public GitHub repository

✅ **Automatic Branch Detection**: Figures out main/master automatically

⏱️ **Network Dependent**: Speed depends on internet connection

## Troubleshooting

### Clone shows no files
- Check internet connection
- Repo might not have key files (README, etc.)
- GitHub API might be rate-limited

### "Accès réseau limité" message
- Network request failed
- Repo doesn't have the expected files
- GitHub API unavailable

### File size shows as 14 bytes
- Old cached result, rebuild with `make clean && make`

## Git Commands Reference

```bash
git clone <url> [dest]        # Clone a repository (NEW: downloads real files!)
git add <files>               # Stage files (simulated)
git commit -m "message"       # Create commit (simulated)
git log [n]                   # Show commit history
git status                    # Show repo status
git branch                    # List branches
git checkout <branch>         # Switch branch
git remote                    # Show remote URLs
```

## File Structure After Clone

```
/myrepo
├── .git/                    # Git metadata
│   ├── objects/            # (empty, not a real git repo)
│   └── refs/               # (empty, not a real git repo)
├── README                  # Downloaded from GitHub
├── LICENSE                 # Downloaded from GitHub
├── Makefile                # Downloaded from GitHub
├── .gitignore              # Downloaded from GitHub
└── [other downloaded files]
```

## Performance Notes

- Initial clone: 2-5 seconds (depends on file sizes)
- Subsequent clones: Create new containers or directories
- Large repos: Linux kernel Makefile is 72KB - still fast

## Advanced Usage

### Clone to Specific Location
```bash
git clone https://github.com/user/repo.git /custom/path/myrepo
```

### Clone Multiple Repos
```bash
git clone https://github.com/octocat/Hello-World.git hello
git clone https://github.com/torvalds/linux.git linux
git clone https://github.com/python/cpython.git python
ls
# hello, linux, python directories visible
```

### Navigate Cloned Repos
```bash
cd myrepo
ls                    # List files in /myrepo
cat README            # View README from clone
cd ..
ls                    # Back to root
```

## Implementation Details

- **Download Tool**: curl (installed by default on macOS/Linux)
- **API**: GitHub API for branch detection
- **Method**: HTTP download from `raw.githubusercontent.com`
- **Storage**: Direct integration into CSFS using `fs_add_file()`
- **Temporary Files**: `/tmp/` staging area (cleaned up automatically)

## Security Notes

✅ No credentials required (public repos only)
✅ HTTPS used for all downloads
✅ No code execution during clone
❌ Can't clone private repositories without credentials

## Next Steps / Future Plans

- [ ] Full tar.gz download and extraction
- [ ] Support for private repos with tokens
- [ ] Recursive directory structure downloads
- [ ] Configurable file lists per repo type
- [ ] Bandwidth limiting and progress bars
- [ ] Parallel downloads for speed

## Questions or Issues?

See the main README.md for architecture details or GIT_CLONING_FEATURE.md for implementation details.
