from django.db import models


class BrokenEntry(models.Model):
    title = models.CharField(max_length=50)
