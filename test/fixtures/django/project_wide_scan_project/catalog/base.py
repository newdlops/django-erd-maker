from django.db import models


class BaseRecord(models.Model):
    title = models.CharField(max_length=120)
