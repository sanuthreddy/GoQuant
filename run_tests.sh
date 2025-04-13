#!/bin/bash

# Clean existing build folder if it exists
echo "Cleaning existing build directory..."
rm -rf build

# Install dependencies
echo "Installing Conan dependencies..."
conan install . \
    --output-folder=build \
    --build=missing

# Configure with Meson
echo "Configuring project with Meson..."
cd build && meson setup \
    --native-file conan_meson_native.ini \
    -Dis_test=true ..

# Build the project
echo "Building the project..."
meson compile

# Copy .env.example to .env in the build directory (if applicable)
if [ -f ../.env.example ]; then
    echo "Copying .env.example to .env..."
    cp ../.env.example .env
else
    echo ".env.example not found. Creating a default .env setup..."

    # Check if Redis is running on port 6379 or 6380
    echo "Checking Redis port availability..."
    if nc -z 127.0.0.1 6379; then
        redis_port=6379
    elif nc -z 127.0.0.1 6380; then
        redis_port=6380
    else
        echo "Warning: Neither port 6379 nor 6380 is active. Defaulting to 6379."
        redis_port=6379
    fi
    cat > .env <<EOL
# Default environment variables
REDIS_HOST=localhost
REDIS_PORT=$redis_port
EOL
    echo "Default .env file created with REDIS_PORT=$redis_port"
fi

# Run the tests
echo "Running tests..."
meson test -v