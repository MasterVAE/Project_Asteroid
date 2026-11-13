
import sys
import time
import math
import api as ship


speed = 100


@ship.on("console_message")
def console_message(message):
    x, y = message.split(" ")
    RotateTowards(float(x), float(y))



def RotateTowards(x, y):
    need_angle = math.atan2(y, x)
    if(need_angle < 0):
        need_angle += 2 * math.pi

    while True:
        angle = ship.getAngle()
        diff = need_angle - angle

        if(abs(diff) < 0.02):
            break

        if(diff > math.pi):
            rcs.gyro(-1)
        elif(diff < -math.pi):
            rcs.gyro(1)
        elif(diff > 0):
            rcs.gyro(1)
        else:
            rcs.gyro(-1)

        time.sleep(0.01)

    rcs.gyro(0)
    print("rotated")

def Stop():
    prop.throttle(0)
    vx, vy = ship.getVel()
    print(vx, vy)
    RotateTowards(-vx, -vy)
    while vx * vx + vy * vy > 1:
        prop.throttle(1)
        time.sleep(0.01)
        vx, vy = ship.getVel()
    prop.throttle(0)
    print("stopped")



def FlyTo(x, y):
    Stop()
    RotateTowards(x, y)
    prop.throttle(1)

    vx, vy = ship.getVel() 
    vel = math.sqrt(vx * vx + vy * vy)
    while vel < speed:
        vx, vy = ship.getVel() 
        vel = math.sqrt(vx * vx + vy * vy)
        time.sleep(0.01)

    prop.throttle(0)
    print("cruising speed")

ship.start()
modules = ship.getModules()
prop = ship.Prop("prop_0")
rcs = ship.RCS("rcs_2")


prop.throttle(1)
time.sleep(3)
FlyTo(1000, 1000)


while ship.is_running():
    try:
        time.sleep(0.1)


    except (TimeoutError, RuntimeError) as error:
        print(error, file=sys.stderr)
        break
