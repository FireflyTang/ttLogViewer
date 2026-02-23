# ttLogViewer

A fast and efficient terminal-based log file viewer written in C++.

## Features

- 📖 **Large File Support**: Efficiently view and navigate large log files
- 🔍 **Chain Filtering**: Configure cascading filters to focus on relevant log entries
- ⚡ **Terminal UI**: Lightweight interface that runs in your terminal
- 🎯 **High Performance**: Written in C++ for maximum speed and efficiency

## Installation

```bash
# Build from source
mkdir build
cd build
cmake ..
make
```

## Usage

```bash
# View a log file
ttLogViewer /path/to/logfile.log

# Apply filters (example)
ttLogViewer /path/to/logfile.log --filter "ERROR" --filter "WARNING"
```

## Configuration

Chain filters can be configured to progressively narrow down log entries:

```bash
# Example: Show only ERROR logs from specific service
ttLogViewer app.log -f "service:auth" -f "ERROR"
```

## Building

### Requirements

- C++17 or later
- CMake 3.15+
- (Add other dependencies as needed)

### Build Steps

```bash
git clone <repository-url>
cd ttLogViewer
mkdir build && cd build
cmake ..
make
```

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

[Add your license here]

## Roadmap

- [ ] Basic log file viewing
- [ ] Syntax highlighting
- [ ] Chain filter implementation
- [ ] Search functionality
- [ ] Bookmark support
- [ ] Configuration file support
