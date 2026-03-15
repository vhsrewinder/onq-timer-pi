# OnQ Timer Pi - Claude Code Notes

## CRITICAL: Deployment Structure

**The project uses a symlink for proper deployment:**

### Directory Structure
- **Git repository**: `/home/novapi/onq-timer-pi/` - Source code under version control
- **Runtime symlink**: `/opt/onq-timer-pi` -> `/home/novapi/onq-timer-pi/`
- **Service runs from**: `/opt/onq-timer-pi` (which points to git repo)

### Service Configuration
```
WorkingDirectory=/opt/onq-timer-pi
ExecStart=/usr/bin/node src/index.js
```

### Deployment Process
Since `/opt/onq-timer-pi` is a symlink to the git repo, you only need to deploy to ONE location:

1. Upload file to `/tmp/` first
2. Copy to git repository:
   ```bash
   cp /tmp/file.js /home/novapi/onq-timer-pi/src/streamdeck-manager.js
   ```
3. Restart service: `sudo systemctl restart onq-timer-pi`
4. Commit changes in git directory

**No need to copy to multiple locations** - changes to the git repo are immediately available to the service via the symlink!

## Stream Deck XL Support

### Icon Sizes
- Original: 72x72 pixels
- XL: 96x96 pixels

### Key Mapping (XL)
The 15-key layout is mapped to the bottom-right corner of the XL (8x4 grid):
- Row 1 (XL indices 11-15): Logical buttons 0-4 (+5s, +10s, 1:00, 2:00, 5:00)
- Row 2 (XL indices 19-23): Logical buttons 5-9 (-5s, -10s, LAST, MINS, SECS)
- Row 3 (XL indices 27-31): Logical buttons 10-14 (STATUS, empty, empty, RESET, START/PAUSE/RESUME)

### Translation Functions
- `_translateKeyIndex(logical)`: Converts logical index (0-14) to physical XL index
- `_reverseTranslateKeyIndex(physical)`: Converts physical XL button press to logical index

## Network
- Pi IP: 192.168.45.103
- OnQ Server: 192.168.45.40:3000
- Web UI: http://192.168.45.103:3000

## Service Management
```bash
# Check status
sudo systemctl status onq-timer-pi

# View logs
sudo journalctl -u onq-timer-pi -n 50 --no-pager

# Restart
sudo systemctl restart onq-timer-pi
```

## Dependencies
After pulling code changes, remember to run:
```bash
cd /home/novapi/onq-timer-pi
npm install --production
```
