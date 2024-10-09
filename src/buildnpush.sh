docker rmi syukurdocker/water_dispenser:latest
#docker buildx build --platform=linux/arm64,linux/amd64 --builder=$(docker buildx create --use) --push -t syukurdocker/water_dispenser .
BUILDER=$(docker buildx create --use)
docker buildx build --platform=linux/arm64,linux/amd64 --push -t syukurdocker/water_dispenser .
docker buildx rm $BUILDER

