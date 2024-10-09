docker rmi syukurdocker/water_dispenser:latest
docker buildx create --rm --use --platform=linux/arm64,linux/amd64 --name multi-platform-builder
docker buildx build --platform linux/amd64,linux/arm64 --push -t syukurdocker/water_dispenser .