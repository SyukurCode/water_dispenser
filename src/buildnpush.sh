docker rmi syukurdocker/water_dispenser:latest
docker buildx build --platform linux/amd64,linux/arm64 --push -t syukurdocker/water_dispenser .
