from django.db import models


class Alpha(models.Model):
    name = models.CharField(max_length=50)


class Beta(models.Model):
    alpha = models.ForeignKey("mesh.Alpha", on_delete=models.CASCADE)


class Gamma(models.Model):
    beta = models.ForeignKey("mesh.Beta", on_delete=models.CASCADE)


class Delta(models.Model):
    gamma = models.ForeignKey("mesh.Gamma", on_delete=models.CASCADE)


class Bridge(models.Model):
    alpha = models.ForeignKey("mesh.Alpha", on_delete=models.CASCADE, related_name="bridges")
    delta = models.ForeignKey("mesh.Delta", on_delete=models.CASCADE, related_name="bridges")


class Peripheral(models.Model):
    bridge = models.ForeignKey("mesh.Bridge", on_delete=models.CASCADE)
