"""Physical Q1..Q7 preview; Q2..Q6 represent five 20-percent segments."""
import math


def duties(elapsed_ms, value, brightness=100, speed=100):
    result = [0] * 7
    if value is None or not math.isfinite(value):
        return result
    value = min(100, max(0, value))
    elapsed_ms = int(elapsed_ms * speed / 100)
    for q in range(2, 7):
        cap = 80 if q == 2 else 100
        fill = min(1, max(0, (value - (q - 2) * 20) / 20))
        phase = (elapsed_ms + 3000 - (q - 2) * 300) % 3000
        ramp = (phase if phase <= 600 else 1200 - phase) / 600
        level = 1 if value >= 100 else (ramp * ramp * fill if phase < 1200 else 0)
        result[q - 1] = round(level * cap * brightness / 100)
    return result


class Smoother:
    def __init__(self, alpha=0.35):
        self.value = None
        self.alpha = alpha

    def update(self, value):
        if value is None or not math.isfinite(value):
            self.value = None
        elif self.value is None:
            self.value = value
        else:
            self.value += self.alpha * (value - self.value)
        return self.value
