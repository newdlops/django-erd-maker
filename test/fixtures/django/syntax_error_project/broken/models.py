from django.db import models


class BrokenModel(models.Model):
    title = models.CharField(max_length=100
