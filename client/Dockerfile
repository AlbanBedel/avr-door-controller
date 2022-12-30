# Use the current LTS
FROM ubuntu:latest

# Install the dependencies
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get -y --no-install-recommends install \
    	    python3 python3-serial python3-mysqldb \
	    python3-bcrypt python3-crcmod && \
    rm -rf /var/lib/apt/lists/*

# Create a non privildged user
ARG USER=doors-admin
RUN useradd -U -s /bin/bash $USER

# Copy the python modules and executable
ARG LIBDIR=/usr/local/lib/avr-doors
ENV PYTHONPATH=$LIBDIR

COPY ubus/*.py $LIBDIR/ubus/
COPY AVRDoorCtrl.py AVRDoorsDB.py DB.py $LIBDIR/
COPY avr-doors-admin.py /usr/local/bin

# Configure the container
USER $USER
ENTRYPOINT [ "avr-doors-admin.py" ]
