docker rmi syukurdocker/water_dispenser:latest
docker buildx build --platform=linux/arm64,linux/amd64 --builder=$(docker buildx create --use) --push -t syukurdocker/water_dispenser .
